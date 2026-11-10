#include "diagnostics/session.h"
#include "official_tools.h"
#include "heap_tools.h"
#include "gprof_tools.h"
#include "session.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string_view>

#include <esp_log.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_timer.h>
#include "sdkconfig.h"
#include <mbedtls/platform_util.h>
extern "C" {
#include <mbedtls/constant_time.h>
}

#ifndef CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
#define CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE 0
#endif
#ifndef CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS
#define CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS 0
#endif
#define LUNAREDU_DIAGNOSTIC_SESSION_ENABLED \
  (CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE || CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS)

namespace lunaredu::lunaros::diagnostics {

#if LUNAREDU_DIAGNOSTIC_SESSION_ENABLED
namespace {

constexpr const char *kTag = "local_debug_session";
constexpr uint64_t kChallengeLifetimeMs = 10U * 60U * 1000U;
constexpr uint64_t kSessionLifetimeMs = 15U * 60U * 1000U;
constexpr uint64_t kRetryDelayMs = 2000U;
constexpr uint64_t kLockoutMs = 30000U;
constexpr uint8_t kMaximumFailures = 5;

std::mutex g_mutex;
bool g_available=false;
std::array<char, 7> g_code = {};
std::array<char, 33> g_token = {};
uint64_t g_challenge_expires_ms = 0;
uint64_t g_session_expires_ms = 0;
uint64_t g_retry_after_ms = 0;
uint8_t g_failures = 0;

uint64_t now_ms() {
  return static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
}

uint32_t remaining_ms(uint64_t deadline, uint64_t now) {
  if (deadline <= now) return 0;
  return static_cast<uint32_t>(
      std::min<uint64_t>(deadline - now, UINT32_MAX));
}

bool constant_time_equal(std::string_view left, std::string_view right) {
  return left.size()==right.size() && (left.empty() ||
      mbedtls_ct_memcmp(left.data(),right.data(),left.size())==0);
}

void issue_challenge_locked(uint64_t now) {
  const unsigned code = 100000U + (esp_random() % 900000U);
  std::snprintf(g_code.data(), g_code.size(), "%06u", code);
  g_challenge_expires_ms = now + kChallengeLifetimeMs;
  g_failures = 0;
  g_retry_after_ms = 0;
  ESP_LOGW(kTag,
           "Local debug code: credential=%s (valid for 10 minutes, serial only)",
           g_code.data());
}

void refresh_locked(uint64_t now) {
  if (g_session_expires_ms != 0 && g_session_expires_ms <= now) {
    std::string error;
    (void)internal::official_tools::stop(error);
    (void)internal::heap_tools::stop(error);
    internal::gprof_tools::request_stop();
    g_session_expires_ms = 0;
    mbedtls_platform_zeroize(g_token.data(),g_token.size());
  }
  if (g_session_expires_ms == 0 &&
      g_retry_after_ms <= now &&
      (g_challenge_expires_ms == 0 || g_challenge_expires_ms <= now)) {
    issue_challenge_locked(now);
  }
}

std::string make_token() {
  std::array<char, 33> value = {};
  std::snprintf(value.data(), value.size(), "%08x%08x%08x%08x",
                static_cast<unsigned>(esp_random()),
                static_cast<unsigned>(esp_random()),
                static_cast<unsigned>(esp_random()),
                static_cast<unsigned>(esp_random()));
  return value.data();
}

} // namespace
#endif

void initialize_diagnostic_session() {
#if LUNAREDU_DIAGNOSTIC_SESSION_ENABLED
  std::lock_guard<std::mutex> lock(g_mutex);
  if(g_available)refresh_locked(now_ms());
#endif
}

DiagnosticSessionStatus diagnostic_session_status() {
  DiagnosticSessionStatus status;
#if LUNAREDU_DIAGNOSTIC_SESSION_ENABLED
  std::lock_guard<std::mutex> lock(g_mutex);
  if(!g_available)return status;
  const uint64_t now = now_ms();
  refresh_locked(now);
  status.enabled = true;
  status.active = g_session_expires_ms > now;
  status.challenge_remaining_ms = remaining_ms(g_challenge_expires_ms, now);
  status.session_remaining_ms = remaining_ms(g_session_expires_ms, now);
  status.retry_after_ms = remaining_ms(g_retry_after_ms, now);
#endif
  return status;
}

bool open_diagnostic_session(const std::string &code, std::string &token,
                        std::string &error) {
#if LUNAREDU_DIAGNOSTIC_SESSION_ENABLED
  std::lock_guard<std::mutex> lock(g_mutex);
  if(!g_available){token.clear();error="debug_surface_disabled";return false;}
  const uint64_t now = now_ms();
  refresh_locked(now);
  if (g_retry_after_ms > now) {
    error = "debug_session_rate_limited";
    return false;
  }
  if (code.size() != 6 || !constant_time_equal(code, g_code.data())) {
    ++g_failures;
    g_retry_after_ms = now +
        (g_failures >= kMaximumFailures ? kLockoutMs : kRetryDelayMs);
    if (g_failures >= kMaximumFailures) {
      g_challenge_expires_ms = 0;
    }
    error = "debug_code_invalid";
    return false;
  }
  token = make_token();
  std::snprintf(g_token.data(), g_token.size(), "%s", token.c_str());
  g_session_expires_ms = now + kSessionLifetimeMs;
  g_challenge_expires_ms = 0;
  mbedtls_platform_zeroize(g_code.data(),g_code.size());
  g_failures = 0;
  g_retry_after_ms = 0;
  error.clear();
  return true;
#else
  (void)code;
  token.clear();
  error = "debug_surface_disabled";
  return false;
#endif
}

bool close_diagnostic_session() {
#if LUNAREDU_DIAGNOSTIC_SESSION_ENABLED
  std::lock_guard<std::mutex> lock(g_mutex);
  if(!g_available)return false;
  const uint64_t now = now_ms();
  refresh_locked(now);
  if (g_session_expires_ms <= now) return false;
  std::string error;
  (void)internal::official_tools::stop(error);
  (void)internal::heap_tools::stop(error);
  internal::gprof_tools::request_stop();
  g_session_expires_ms = 0;
  mbedtls_platform_zeroize(g_token.data(),g_token.size());
  issue_challenge_locked(now);
  return true;
#else
  return false;
#endif
}


bool diagnostic_session_enabled() {
#if LUNAREDU_DIAGNOSTIC_SESSION_ENABLED
  std::lock_guard<std::mutex> lock(g_mutex);return g_available;
#else
  return false;
#endif
}

bool diagnostic_session_token_valid(const char *token) {
#if LUNAREDU_DIAGNOSTIC_SESSION_ENABLED
  std::lock_guard<std::mutex> lock(g_mutex);
  if(!g_available)return false;
  const uint64_t now = now_ms();
  refresh_locked(now);
  return token != nullptr && g_session_expires_ms > now &&
         constant_time_equal(token, g_token.data());
#else
  (void)token;
  return false;
#endif
}

bool start_sessions(){
 static const DiagnosticSessionBackend backend{initialize_diagnostic_session,diagnostic_session_status,open_diagnostic_session,close_diagnostic_session,diagnostic_session_enabled,diagnostic_session_token_valid};
 if(!bind_diagnostic_session_backend(backend))return false;
#if LUNAREDU_DIAGNOSTIC_SESSION_ENABLED
 {std::lock_guard<std::mutex> lock(g_mutex);g_available=true;refresh_locked(now_ms());}
#endif
 return true;
}
bool start_official_trace(uint32_t duration_ms,std::string &error) {
#if LUNAREDU_DIAGNOSTIC_SESSION_ENABLED
 std::lock_guard<std::mutex> lock(g_mutex);
 const auto now=now_ms();
 if(g_available)refresh_locked(now);
 if(!g_available || g_session_expires_ms<=now){error="debug_session_required";return false;}
 const auto remaining=remaining_ms(g_session_expires_ms,now);
 if(remaining<100){error="debug_session_expiring";return false;}
 return internal::official_tools::start(std::min(duration_ms,remaining),error,g_session_expires_ms);
#else
 (void)duration_ms;error="debug_session_required";return false;
#endif
}
void stop_sessions(){
#if LUNAREDU_DIAGNOSTIC_SESSION_ENABLED
 std::lock_guard<std::mutex> lock(g_mutex);
 std::string error;
 (void)internal::official_tools::stop(error);
 (void)internal::heap_tools::stop(error);
 internal::gprof_tools::request_stop();
 g_available=false;mbedtls_platform_zeroize(g_code.data(),g_code.size());mbedtls_platform_zeroize(g_token.data(),g_token.size());g_challenge_expires_ms=g_session_expires_ms=g_retry_after_ms=0;g_failures=0;
#endif
}
bool start_heap_trace(uint32_t duration_ms,bool leaks_only,std::string &error){
#if LUNAREDU_DIAGNOSTIC_SESSION_ENABLED
 std::lock_guard<std::mutex> lock(g_mutex);
 const auto now=now_ms();
 if(g_available)refresh_locked(now);
 if(!g_available || g_session_expires_ms<=now){error="debug_session_required";return false;}
 return internal::heap_tools::start(duration_ms,leaks_only,g_session_expires_ms,error);
#else
 (void)duration_ms;(void)leaks_only;error="debug_session_required";return false;
#endif
}
bool start_gprof_trace(uint32_t duration_ms,std::string &error){
#if LUNAREDU_DIAGNOSTIC_SESSION_ENABLED
 std::lock_guard<std::mutex> lock(g_mutex);const auto now=now_ms();
 if(g_available)refresh_locked(now);
 if(!g_available||g_session_expires_ms<=now){error="debug_session_required";return false;}
 return internal::gprof_tools::start(duration_ms,g_session_expires_ms,error);
#else
 (void)duration_ms;error="debug_session_required";return false;
#endif
}
}
