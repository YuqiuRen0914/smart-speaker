#include "lunaros_network_control.h"
#include "platform_link_http.h"
#include <mutex>

#include <esp_log.h>
#include "configuration/device_config.h"
#include "system/identity.h"
#include "lunaros_network_http_client.h"
#include "lunaros_network.h"
#include "lunaros_resource.h"
#include "platform_link_state.h"
#include "platform_link_environment.h"
#include "platform/system.h"

namespace lunaredu::lunaros::platform_link {
namespace {

constexpr const char *kTag = "platform_link";
constexpr uint32_t kHttpTimeoutMs = 8000;

NetworkFetchRequest build_request(const char *method, const std::string &path,
                                  const std::string *body,
                                  bool authenticated) {
  NetworkFetchRequest request;
  request.maximum_response_bytes = 64U * 1024U;
  request.method = method;
  request.url = std::string(platform_base_url()) + path;
  request.timeout_ms = static_cast<int>(kHttpTimeoutMs);
  request.transport_security = NetworkTransportSecurity::RequireTls;
  if (authenticated) {
    request.headers.push_back({"X-Hardware-Serial", hardware_serial_number()});
    request.headers.push_back(
        {"Authorization", "Bearer " + device_cloud_credential()});
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
      ESP_LOGW(kTag, "%s %s returned invalid JSON: %s", method, path.c_str(),
           error.c_str());
      return false;
    }
  }
  if (response.status_code < 200 || response.status_code >= 300) {
    ESP_LOGW(kTag, "%s %s rejected: status=%d", method, path.c_str(),
         response.status_code);
    return false;
  }
  return true;
}

void report_platform_transport(const std::string &path,bool completed,
                               const NetworkResponse &response,
                               bool operation_succeeded) {
  // A local scheduling deferral contains no network observation. Preserve
  // the last peer status instead of falsely displaying a disconnect/error.
  if (response.deferred) return;
  const char *operation=path=="/api/link/device-enrollment/open" ? "enrollment" :
      path=="/api/link/device-runtime/presence" ? "presence" :
      path.find("pairing-sessions")!=std::string::npos ? "pairing" : "other";
  const char *code=operation_succeeded ? "" : !completed ? "platform_transport_unavailable" :
      response.status_code==401 ? "device_credential_rejected" :
      response.status_code==409 ? "device_identity_conflict" :
      response.status_code==404 ? "platform_endpoint_not_found" :
      response.status_code==429 ? "platform_rate_limited" :
      response.status_code>=500 ? "platform_service_unavailable" :
      response.status_code>=400 ? "platform_request_rejected" : "platform_response_invalid";
  update_platform_request_diagnostics(operation,code);
  if (!completed && response.status_code < 0) {
    update_platform_connection(PlatformLinkPlatformConnectionState::Disconnected,
                               response.status_code,
                               response.error_detail.empty()
                                   ? "platform_transport_unreachable"
                                   : response.error_detail);
    return;
  }
  if (operation_succeeded) {
    update_platform_connection(PlatformLinkPlatformConnectionState::Connected,
                               response.status_code);
    return;
  }
  update_platform_connection(PlatformLinkPlatformConnectionState::Error,
                             response.status_code,
                             response.error_detail.empty()
                                 ? "platform_request_rejected"
                                 : response.error_detail);
}

} // namespace

uint32_t monotonic_ms() {
  return system_monotonic_ms();
}

bool platform_is_production() {
  if(runtime_config().role_firmware_updates_only){
    const auto origin=platform_link_pending_ota_origin();
    if(!origin.empty())return remote_origin_is_production(origin,runtime_config().production_base_url);
  }
  const auto endpoint = runtime_config().remote_base_url;
  if (endpoint != nullptr)
    return remote_origin_is_production(endpoint(), runtime_config().production_base_url);
  const std::string &environment = device_cloud_config().environment;
  // An unprovisioned or migrated device must always target the official
  // service. Development endpoints require an explicit persisted selection
  // and use the same TLS transport as production.
  return environment != "development" && environment != "DEVELOPMENT";
}

std::string platform_base_url() {
  const PlatformLinkConfig &config = runtime_config();
  const auto canonical = [&config](const std::string &value) {
    static std::mutex mutex;
    static std::string source, production, projected;
    static bool valid = false;
    std::lock_guard<std::mutex> lock(mutex);
    if (!valid || source != value || production != config.production_base_url) {
      std::string origin;
      projected = normalize_development_origin(value, origin, config.production_base_url) ? origin : value;
      source = value;
      production = config.production_base_url;
      valid = true;
    }
    return projected;
  };
  if(config.role_firmware_updates_only){const auto origin=platform_link_pending_ota_origin();if(!origin.empty())return canonical(origin);}
  if (config.remote_base_url != nullptr) return canonical(config.remote_base_url());
  if (platform_is_production()) {
    return config.production_base_url.empty()
               ? kDefaultProductionDeviceOrigin
               : canonical(config.production_base_url);
  }
  const auto saved = device_cloud_config().development_base_url;
  if (!saved.empty()) return canonical(saved);
  return config.development_base_url.empty()
             ? kDefaultDevelopmentDeviceOrigin
             : canonical(config.development_base_url);
}

bool ensure_network_clock(std::string &error) {
  if (network_fast_snapshot().clock == NetworkClockState::Valid) {
    error.clear();
    return true;
  }
  return synchronize_network_clock_blocking(
      kPlatformLinkTimeSyncTimeoutMs, error);
}

void configure_resource_transport() {
  lunaredu::lunaros::configure_resource_transport({
      .base_url = platform_base_url(),
      .device_serial = hardware_serial_number(),
      .bearer_token = device_cloud_credential(),
  });
}

bool perform_request(const char *method, const std::string &path,
                     const std::string *body, JsonDocument *response_doc,
                     int *status_code) {
  NetworkResponse response;
  const bool completed = network_fetch_resource(
      build_request(method, path, body, true), &response);
  if (!completed) {
    report_platform_transport(path,false, response, false);
    if (status_code != nullptr) {
      *status_code = response.status_code;
    }
    if (response.deferred) return false;
    ESP_LOGW(kTag, "%s %s failed: status=%d error=%d detail=%s", method,
         path.c_str(), response.status_code, response.last_error,
         response.error_detail.empty() ? "-" : response.error_detail.c_str());
    return false;
  }
  if (status_code != nullptr) {
    *status_code = response.status_code;
  }
  const bool succeeded =
      parse_successful_response(method, path, response, response_doc);
  report_platform_transport(path,true, response, succeeded);
  return succeeded;
}

bool perform_public_request(const char *method, const std::string &path,
                            const std::string *body,
                            JsonDocument *response_doc, int *status_code) {
  NetworkResponse response;
  const bool completed = network_fetch_resource(
      build_request(method, path, body, false), &response);
  if (!completed) {
    report_platform_transport(path,false, response, false);
    if (status_code != nullptr) {
      *status_code = response.status_code;
    }
    if (response.deferred) return false;
    ESP_LOGW(kTag, "%s %s failed: status=%d error=%d detail=%s", method,
         path.c_str(), response.status_code, response.last_error,
         response.error_detail.empty() ? "-" : response.error_detail.c_str());
    return false;
  }
  if (status_code != nullptr) {
    *status_code = response.status_code;
  }
  const bool succeeded =
      parse_successful_response(method, path, response, response_doc);
  report_platform_transport(path,true, response, succeeded);
  return succeeded;
}

} // namespace lunaredu::lunaros::platform_link
