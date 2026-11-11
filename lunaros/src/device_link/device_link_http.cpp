#include "device_link/device_link_http.h"

#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/persistence/settings.h"
#include "lunaredu_lunaros/persistence/identity.h"
#include "lunaredu_lunaros/network/http_client.h"
#include "lunaredu_lunaros/network/network.h"
#include "lunaredu_lunaros/delivery/resources.h"
#include "device_link/device_link_state.h"
#include "kernel/system_primitives.h"

namespace lunaredu::lunaros::device_link {
namespace {

constexpr const char *kTag = "device_link";
constexpr const char *kFallbackDevelopmentPlatformUrl =
    "http://board.lunaredu.localhost";
constexpr const char *kFallbackProductionPlatformUrl =
    "https://board.lunaredu.cn";
constexpr uint32_t kHttpTimeoutMs = 8000;

NetworkFetchRequest build_request(const char *method, const std::string &path,
                                  const std::string *body,
                                  bool authenticated) {
  NetworkFetchRequest request;
  request.maximum_response_bytes = 64U * 1024U;
  request.method = method;
  request.url = std::string(platform_base_url()) + path;
  request.timeout_ms = static_cast<int>(path == "/api/link/device-runtime/presence" ? 12000 : kHttpTimeoutMs);
  request.transport_security =
      platform_is_production()
          ? NetworkTransportSecurity::RequireTls
          : NetworkTransportSecurity::AllowPlaintextDevelopment;
  if (authenticated) {
    request.headers.push_back({"X-Hardware-Serial", hardware_serial_number()});
    request.headers.push_back(
        {"Authorization", "Bearer " + device_config().cloud.credential});
  }
  if (body != nullptr) {
    request.headers.push_back({"Content-Type", "application/json"});
    request.body = *body;
  }
  return request;
}

bool parse_successful_response(const char *method, const std::string &path,
                               const NetworkResponse &response,
                               JsonDocument *response_doc) {
  if (response_doc != nullptr && !response.body.empty()) {
    const DeserializationError error =
        deserializeJson(*response_doc, response.body);
    if (error) {
      LOGW(kTag, "%s %s returned invalid JSON: %s", method, path.c_str(),
           error.c_str());
      return false;
    }
  }
  if (response.status_code < 200 || response.status_code >= 300) {
    LOGW(kTag, "%s %s rejected: status=%d", method, path.c_str(),
         response.status_code);
    return false;
  }
  return true;
}

void report_platform_transport(bool completed,
                               const NetworkResponse &response,
                               bool operation_succeeded) {
  if (!completed && response.status_code < 0) {
    update_platform_connection(DeviceLinkPlatformConnectionState::Disconnected,
                               response.status_code,
                               response.error_detail.empty()
                                   ? "platform_transport_unreachable"
                                   : response.error_detail);
    return;
  }
  if (operation_succeeded) {
    update_platform_connection(DeviceLinkPlatformConnectionState::Connected,
                               response.status_code);
    return;
  }
  update_platform_connection(DeviceLinkPlatformConnectionState::Error,
                             response.status_code,
                             response.error_detail.empty()
                                 ? "platform_request_rejected"
                                 : response.error_detail);
}

} // namespace

uint32_t monotonic_ms() {
  return internal::system_monotonic_ms();
}

bool platform_is_production() {
  const std::string &environment = device_config().cloud.environment;
  // An unprovisioned or migrated device must always target the official
  // service. Plain HTTP development mode is enabled only by an explicit
  // persisted selection.
  return environment != "development" && environment != "DEVELOPMENT";
}

const char *platform_base_url() {
  const DeviceLinkConfig &config = runtime_config();
  if (platform_is_production()) {
    return config.production_base_url.empty()
               ? kFallbackProductionPlatformUrl
               : config.production_base_url.c_str();
  }
  return config.development_base_url.empty()
             ? kFallbackDevelopmentPlatformUrl
             : config.development_base_url.c_str();
}

bool ensure_network_clock(std::string &error) {
  if (network_snapshot().clock == NetworkClockState::Valid) {
    error.clear();
    return true;
  }
  return synchronize_network_clock_blocking(
      kDeviceLinkTimeSyncTimeoutMs, error);
}

void configure_resource_transport() {
  lunaredu::lunaros::configure_resource_transport({
      .base_url = platform_base_url(),
      .device_serial = hardware_serial_number(),
      .bearer_token = device_config().cloud.credential,
      .allow_plaintext_development = !platform_is_production(),
  });
}

bool perform_request(const char *method, const std::string &path,
                     const std::string *body, JsonDocument *response_doc,
                     int *status_code) {
  NetworkResponse response;
  const bool completed = network_fetch_resource(
      build_request(method, path, body, true), &response);
  if (!completed) {
    report_platform_transport(false, response, false);
    if (status_code != nullptr) {
      *status_code = response.status_code;
    }
    LOGW(kTag, "%s %s failed: status=%d error=%d detail=%s", method,
         path.c_str(), response.status_code, response.last_error,
         response.error_detail.empty() ? "-" : response.error_detail.c_str());
    return false;
  }
  if (status_code != nullptr) {
    *status_code = response.status_code;
  }
  const bool succeeded =
      parse_successful_response(method, path, response, response_doc);
  report_platform_transport(true, response, succeeded);
  return succeeded;
}

bool perform_public_request(const char *method, const std::string &path,
                            const std::string *body,
                            JsonDocument *response_doc, int *status_code) {
  NetworkResponse response;
  const bool completed = network_fetch_resource(
      build_request(method, path, body, false), &response);
  if (!completed) {
    report_platform_transport(false, response, false);
    if (status_code != nullptr) {
      *status_code = response.status_code;
    }
    LOGW(kTag, "%s %s failed: status=%d error=%d detail=%s", method,
         path.c_str(), response.status_code, response.last_error,
         response.error_detail.empty() ? "-" : response.error_detail.c_str());
    return false;
  }
  if (status_code != nullptr) {
    *status_code = response.status_code;
  }
  const bool succeeded =
      parse_successful_response(method, path, response, response_doc);
  report_platform_transport(true, response, succeeded);
  return succeeded;
}

} // namespace lunaredu::lunaros::device_link
