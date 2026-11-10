#include "lunaros_network_tls.h"
#include "configuration/settings.h"
#include "tls_session_cache.h"
#include "tls_policy_fields_json.h"
#include "platform/system.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cctype>
#include <mutex>

namespace lunaredu::lunaros {
namespace {
std::mutex policy_mutex;
std::shared_ptr<const NetworkTlsPolicy> cached;
const auto standard = std::make_shared<const NetworkTlsPolicy>();
constexpr size_t kRecordMax = 6144;
constexpr uint32_t kStorageRetryMs = 5000;
bool loading = false, saving = false, retry_storage = false;
uint32_t failed_read_at = 0, generation = 0;
std::string host_of(const std::string &origin) {
  const size_t start = origin.find("://") + 3;
  return origin.substr(start, origin.find(':', start) - start);
}
bool valid_policy(const NetworkTlsPolicy &policy) {
  std::string normalized;
  if (!network_tls_normalize_origin(policy.origin, normalized) || normalized != policy.origin) return false;
  if (policy.mode == "custom_ca") return policy.ca_pem.size() <= kNetworkTlsCaMaxBytes &&
      policy.ca_pem.find('\0') == std::string::npos && network_tls_validate_ca(policy.ca_pem);
  return (policy.mode == "standard" || policy.mode == "skip_verify") && policy.ca_pem.empty();
}
}
bool network_tls_normalize_origin(const std::string &value, std::string &result) {
  result.clear();
  if (value.size() > 256 || value.rfind("https://", 0) != 0) return false;
  std::string authority = value.substr(8);
  if (!authority.empty() && authority.back() == '/') authority.pop_back();
  if (authority.empty() || authority.find_first_of("/@?#\\\r\n\t ") != std::string::npos) return false;
  const size_t colon = authority.find(':');
  std::string host = authority.substr(0, colon);
  if (host.empty() || host.front() == '.' || host.back() == '.') return false;
  for (unsigned char c : host) if (!std::isalnum(c) && c != '.' && c != '-') return false;
  std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (host == "device.lunaredu.cn" || host == "device.lunaredu.localhost")
    host.replace(0,7,"gateway.");
  std::string port;
  if (colon != std::string::npos) {
    port = authority.substr(colon + 1);
    if (port.empty() || port.size() > 5) return false;
    unsigned parsed = 0;
    for (unsigned char c : port) { if (!std::isdigit(c)) return false; parsed = parsed * 10 + c - '0'; }
    if (parsed == 0 || parsed > 65535) return false;
    port = parsed == 443 ? "" : ":" + std::to_string(parsed);
  }
  result = "https://" + host + port;
  return true;
}
std::shared_ptr<const NetworkTlsPolicy> network_tls_saved_policy() {
  uint32_t observed_generation;
  {
    std::lock_guard<std::mutex> lock(policy_mutex);
    if (cached) return cached;
    if (loading || saving || (retry_storage &&
        system_monotonic_ms() - failed_read_at < kStorageRetryMs)) return standard;
    loading = true;
    observed_generation = generation;
  }
  std::string record, error;
  const auto read = settings_read_string("net_tls", "policy", kRecordMax, record, error);
  std::shared_ptr<const NetworkTlsPolicy> next = standard;
  if (read == SettingsReadResult::Found) {
    JsonDocument doc;
    if (!deserializeJson(doc, record)) {
      auto policy = std::make_shared<NetworkTlsPolicy>();
      if (internal::read_tls_policy_fields(doc.as<JsonVariantConst>(), *policy)) {
        // Keep the explicitly selected trust policy when its retired official
        // host migrates to Gateway; do not silently fall back to another CA.
        if (policy->origin.rfind("https://device.lunaredu.", 0) == 0) {
          std::string migrated;
          if (network_tls_normalize_origin(policy->origin,migrated)) policy->origin=std::move(migrated);
        }
        if (valid_policy(*policy)) next = std::move(policy);
      }
    }
  }
  std::lock_guard<std::mutex> lock(policy_mutex);
  loading = false;
  if (observed_generation == generation && !saving) {
    // Bad/missing records are stable until this owner commits a replacement.
    // Storage faults are transient and retry at a bounded rate, fail closed.
    if (read == SettingsReadResult::Failed) {
      retry_storage = true;
      failed_read_at = system_monotonic_ms();
    } else {
      cached = std::move(next);
      retry_storage = false;
    }
  }
  return cached ? cached : standard;
}
std::shared_ptr<const NetworkTlsPolicy> network_tls_policy_for_url(
    const std::string &url, const std::string &service_origin) {
  const auto policy = network_tls_saved_policy();
  const size_t scheme = url.find("://");
  if (scheme == std::string::npos) return standard;
  const std::string protocol = url.substr(0, scheme);
  if (protocol != "https" && protocol != "wss" && protocol != "mqtts") return standard;
  const size_t end = url.find_first_of("/?#", scheme + 3);
  std::string destination;
  if (!network_tls_normalize_origin("https://" + url.substr(scheme + 3, end - scheme - 3), destination)) return standard;
  const auto &fallback = standard;
  if (policy->mode == "standard") return fallback;
  if (protocol != "mqtts") return destination == policy->origin ? policy : fallback;
  // Bootstrap may specify a distinct MQTT port, but never extend trust to an
  // arbitrary broker hostname supplied in its response.
  std::string origin;
  if (!network_tls_normalize_origin(service_origin, origin) || origin != policy->origin || host_of(destination) != host_of(origin)) return fallback;
  return policy;
}
bool network_tls_save_policy(NetworkTlsPolicy policy, std::string &error) {
  error.clear();
  std::string origin;
  if (!network_tls_normalize_origin(policy.origin, origin)) { error = "tls_invalid_origin"; return false; }
  policy.origin = origin;
  if (!valid_policy(policy)) { error = "tls_invalid_policy_or_ca"; return false; }
  JsonDocument doc;
  doc["origin"] = policy.origin; doc["mode"] = policy.mode; doc["ca_pem"] = policy.ca_pem;
  std::string record; serializeJson(doc, record);
  if (record.size() > kRecordMax) { error = "tls_ca_too_large"; return false; }
  auto next = std::make_shared<const NetworkTlsPolicy>(std::move(policy));
  {
    std::lock_guard<std::mutex> lock(policy_mutex);
    if (saving) { error = "tls_policy_busy"; return false; }
    saving = true;
    ++generation;
  }
  const bool saved = settings_set_string("net_tls", "policy", record);
  std::shared_ptr<const NetworkTlsPolicy> previous;
  if (saved) {
    {
      std::lock_guard<std::mutex> lock(policy_mutex);
      previous = std::move(cached); // Release old storage outside the view lock.
      cached = std::move(next);
      retry_storage = false;
    }
    // Keep the write reservation, but never the view lock, through SDK cleanup.
    invalidate_http_tls_session();
  }
  {
    std::lock_guard<std::mutex> lock(policy_mutex);
    saving = false;
  }
  if (!saved) error = "tls_save_failed";
  return saved;
}
} // namespace lunaredu::lunaros
