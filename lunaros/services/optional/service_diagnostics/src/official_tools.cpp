#include "official_tools.h"
#include "trace_retention.h"

#if CONFIG_LUNAROS_DIAGNOSTIC_APPTRACE || CONFIG_LUNAROS_DIAGNOSTIC_SYSTEMVIEW || CONFIG_LUNAROS_DIAGNOSTIC_COREDUMP_UART || CONFIG_LUNAROS_DIAGNOSTIC_COREDUMP_FLASH
#include <array>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <limits>
#include "kernel/static_mutex.h"
#include <esp_timer.h>
#include <esp_err.h>
#if CONFIG_LUNAROS_DIAGNOSTIC_APPTRACE
#include <esp_app_trace.h>
#endif
#if CONFIG_LUNAROS_DIAGNOSTIC_SYSTEMVIEW
#include <esp_trace.h>
#include <SEGGER_SYSVIEW.h>
// IDF 6.1 defaults the external encoder name to "ext"; this component registers
// "sysview". The SDK calls this before app_main and retains its own buffers.
extern "C" esp_trace_open_params_t esp_trace_get_user_params(void) {
#if CONFIG_LUNAROS_DIAGNOSTIC_FIELD
  return {nullptr,"sysview",nullptr,"lunar_retained",nullptr};
#else
  return {nullptr,"sysview",nullptr,"apptrace",nullptr};
#endif
}
#endif

namespace lunaredu::lunaros::internal::official_tools {
namespace {
StaticMutex mutex;
bool ready = false;
bool running = false;
std::atomic<bool> observing{false};
uint64_t deadline_ms = 0;
uint32_t generation = 0;
uint32_t emitted = 0;
uint32_t dropped = 0;
int last_error = ESP_OK;

uint64_t now_ms() { return uint64_t(esp_timer_get_time()) / 1000; }
bool stop_locked(std::string &error) {
  deadline_ms = 0; // Close admission even if the SDK still needs retirement retries.
#if CONFIG_LUNAROS_DIAGNOSTIC_SYSTEMVIEW
  if (running) {
    SEGGER_SYSVIEW_Stop();
    last_error = SEGGER_SYSVIEW_Started() ? ESP_FAIL : ESP_OK;
    if (last_error != ESP_OK) { error="official_trace_stop_failed"; return false; }
  }
#endif
  running = false;
  observing.store(false,std::memory_order_release);
  trace_retention::finish();
  error.clear();
  return true;
}
}
void resume() {
  StaticLockGuard lock(mutex);
#if CONFIG_LUNAROS_DIAGNOSTIC_SYSTEMVIEW
  if (!running && esp_trace_get_active_handle()) SEGGER_SYSVIEW_Stop();
#endif
  ready = true;
}
bool shutdown(std::string &error) {
  StaticLockGuard lock(mutex);
  if (!stop_locked(error)) return false;
  if (!trace_retention::shutdown(error)) return false;
  ready = false;
  return true;
}
bool start(uint32_t duration_ms, std::string &error, uint64_t authorization_deadline_ms) {
  StaticLockGuard lock(mutex);
  if (!ready) { error="official_trace_service_stopped"; return false; }
  if (duration_ms < 100 || duration_ms > 900'000) { error="official_trace_invalid_duration"; return false; }
  if (running) { error="official_trace_busy"; return false; }
  if (generation == std::numeric_limits<uint32_t>::max()) { error="official_trace_generation_exhausted"; return false; }
  const auto started=now_ms();
  if (started >= authorization_deadline_ms) { error="official_trace_session_expired"; return false; }
#if CONFIG_LUNAROS_DIAGNOSTIC_SYSTEMVIEW
  if (!esp_trace_get_active_handle()) { error="official_trace_unavailable"; return false; }
  if (!trace_retention::begin("systemview",error,std::min(started+duration_ms,authorization_deadline_ms))) return false;
  SEGGER_SYSVIEW_Start();
  last_error = SEGGER_SYSVIEW_Started() ? ESP_OK : ESP_FAIL;
  if (last_error != ESP_OK) { trace_retention::finish();error="official_trace_start_failed"; return false; }
#elif CONFIG_LUNAROS_DIAGNOSTIC_APPTRACE
  if (!trace_retention::begin("apptrace",error,std::min(started+duration_ms,authorization_deadline_ms))) return false;
#elif !CONFIG_LUNAROS_DIAGNOSTIC_APPTRACE
  error="official_trace_not_configured"; return false;
#endif
  deadline_ms = std::min(started+duration_ms,authorization_deadline_ms);
  running = true;
  observing.store(true,std::memory_order_release);
  if (now_ms() >= deadline_ms) {
    if (stop_locked(error)) error="official_trace_expired_during_start";
    return false;
  }
  ++generation;
  emitted = dropped = 0;
  last_error = ESP_OK;
  error.clear();
  return true;
}
bool stop(std::string &error) { StaticLockGuard lock(mutex); return stop_locked(error); }
bool needs_poll() { return observing.load(std::memory_order_acquire); }
void poll(bool session_active) {
  StaticLockGuard lock(mutex);
  if(running)trace_retention::drain();
  if (running && (!session_active || now_ms() >= deadline_ms || trace_retention::saturated())) {
    std::string error;
    (void)stop_locked(error); // Keep running state on failure so retirement retries.
  }
}
Status status() {
  StaticLockGuard lock(mutex);
  Status result;
#if CONFIG_LUNAROS_DIAGNOSTIC_APPTRACE
  result.trace_mode="apptrace_jtag";
  result.available=ready;
  result.host_connected=ready && esp_apptrace_host_is_connected();
#elif CONFIG_LUNAROS_DIAGNOSTIC_SYSTEMVIEW
#if CONFIG_LUNAROS_DIAGNOSTIC_FIELD
  result.trace_mode="systemview_retained";
#else
  result.trace_mode="systemview_apptrace_jtag";
#endif
  const auto handle=esp_trace_get_active_handle();
  result.available=ready && handle;
  result.host_connected=result.available && esp_trace_is_host_connected(handle);
#endif
#if CONFIG_LUNAROS_DIAGNOSTIC_COREDUMP_FLASH
  result.core_dump="flash_elf_sha256";
#elif CONFIG_LUNAROS_DIAGNOSTIC_COREDUMP_UART
  result.core_dump="uart_elf_sha256";
#endif
  const auto now=now_ms();
  result.active=running && now < deadline_ms;
  result.retiring=running && now >= deadline_ms;
  result.remaining_ms=result.active ? uint32_t(deadline_ms-now) : 0;
  result.generation=generation;
  result.emitted=emitted;
  result.dropped=dropped;
  result.last_error=last_error;
  return result;
}
void record(const DebugLogRecord &event, uint32_t capture_generation) {
  StaticLockGuard lock(mutex);
  if (!ready || !running || event.uptime_ms >= deadline_ms || now_ms() >= deadline_ms) return;
  // Versioned bounded ASCII records: decimal IDs retain their full 64 bits.
  // Tabs delimit sanitized operation/error identifiers; no pointers or payloads.
  std::array<char, 320> line{};
  const int length=std::snprintf(line.data(),line.size(),
      "LDT1\t%lu\t%lu\t%llu\t%llu\t%llu\t%lu\t%u\t%.79s\t%.79s\n",
      static_cast<unsigned long>(generation),static_cast<unsigned long>(capture_generation),
      static_cast<unsigned long long>(event.uptime_ms),
      static_cast<unsigned long long>(event.trace_id),
      static_cast<unsigned long long>(event.parent_trace_id),
      static_cast<unsigned long>(event.request_sequence),unsigned(event.phase),
      event.message.data(),event.message.data()+80);
  if (length < 0 || size_t(length) >= line.size()) { ++dropped; return; }
#if CONFIG_LUNAROS_DIAGNOSTIC_APPTRACE
  (void)trace_retention::write(line.data(),size_t(length));
  last_error=esp_apptrace_write(line.data(),uint32_t(length),0); // Never wait for host.
  if (last_error != ESP_OK) { ++dropped; return; }
#elif CONFIG_LUNAROS_DIAGNOSTIC_SYSTEMVIEW
  // SystemView print strings are limited to 128 bytes. Three correlated lines
  // retain full IDs and both 79-byte identifiers without silent truncation.
  std::snprintf(line.data(),line.size(),"LDT1 m %lu %lu %llu %llu %llu %lu %u",
      static_cast<unsigned long>(generation),static_cast<unsigned long>(capture_generation),
      static_cast<unsigned long long>(event.uptime_ms),
      static_cast<unsigned long long>(event.trace_id),
      static_cast<unsigned long long>(event.parent_trace_id),
      static_cast<unsigned long>(event.request_sequence),unsigned(event.phase));
  SEGGER_SYSVIEW_Print(line.data());
  std::snprintf(line.data(),line.size(),"LDT1 o %llu %.79s",
      static_cast<unsigned long long>(event.trace_id),event.message.data());
  SEGGER_SYSVIEW_Print(line.data());
  std::snprintf(line.data(),line.size(),"LDT1 e %llu %.79s",
      static_cast<unsigned long long>(event.trace_id),event.message.data()+80);
  SEGGER_SYSVIEW_Print(line.data());
#else
  return;
#endif
  ++emitted;
}
}
#endif
