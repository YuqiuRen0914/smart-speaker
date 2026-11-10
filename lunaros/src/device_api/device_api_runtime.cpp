#include "lunaredu_lunaros/device_api/device_api.h"

#include <atomic>

#include "lunaredu_lunaros/network/network.h"
#include "lunaredu_lunaros/kernel/service_host.h"
#include "lunaredu_lunaros/kernel/service_ids.h"
#include "lunaredu_lunaros/kernel/service_executor.h"
#include "device_api/http/embedded_http_server.h"
#include "device_api/http/device_web_routes.h"
#include "kernel/system_primitives.h"

namespace lunaredu::lunaros {
namespace {

bool g_device_api_configured = false;
constexpr uint32_t kHostActivityWindowMs = 12'000;
std::atomic<DeviceHostConnectionState> g_host_connection_state{
    DeviceHostConnectionState::Disconnected};
std::atomic<uint32_t> g_host_last_activity_ms{0};

ServiceStartResult start_device_api_service(void *) {
  std::string error;
  if (!start_device_api(error)) {
    return {ServiceStartDisposition::Failed,
            error.empty() ? "device_api_start_failed" : error};
  }
  return {ServiceStartDisposition::Ready, "device_api_ready"};
}

bool stop_device_api_service(void *, std::string &error) {
  return stop_device_api(error);
}

} // namespace

void setup_device_api() {
  if (g_device_api_configured) {
    return;
  }

  register_device_http_routes();
  setup_device_web_routes();
  g_device_api_configured = true;
}

bool start_device_api(std::string &error) {
  error.clear();
  setup_device_api();
  if (embedded_http_server_handle() == nullptr) {
    (void)start_embedded_http_server_if_needed();
  }
  if (embedded_http_server_handle() == nullptr) {
    error = "device_api_http_start_failed";
    report_device_host_connection(DeviceHostConnectionState::Error);
    return false;
  }
  return true;
}

bool stop_device_api(std::string &error) {
  const bool stopped = stop_embedded_http_server(error);
  report_device_host_connection(stopped ? DeviceHostConnectionState::Disconnected
                                        : DeviceHostConnectionState::Error);
  return stopped;
}

void report_device_host_connection(DeviceHostConnectionState state) {
  g_host_connection_state.store(state, std::memory_order_release);
  g_host_last_activity_ms.store(
      state == DeviceHostConnectionState::Disconnected
          ? 0
          : internal::system_monotonic_ms(),
      std::memory_order_release);
}

DeviceHostConnectionSnapshot device_host_connection_snapshot() {
  DeviceHostConnectionSnapshot snapshot;
  snapshot.state = g_host_connection_state.load(std::memory_order_acquire);
  snapshot.last_activity_ms =
      g_host_last_activity_ms.load(std::memory_order_acquire);
  if (snapshot.state != DeviceHostConnectionState::Disconnected &&
      snapshot.last_activity_ms != 0 &&
      internal::system_monotonic_ms() - snapshot.last_activity_ms >
          kHostActivityWindowMs) {
    snapshot.state = DeviceHostConnectionState::Disconnected;
  }
  return snapshot;
}

ServiceNode device_api_service_node() {
  static constexpr ServiceDependency kDependencies[] = {
      {kNetworkRuntimeServiceId, true},
      {kStorageServiceId, false},
  };
  return {
      {kDeviceApiServiceId, "Device API", kNetworkServiceExecutor,
       ServiceCriticality::Optional, kDependencies,
       sizeof(kDependencies) / sizeof(kDependencies[0])},
      start_device_api_service,
      stop_device_api_service,
      nullptr,
      nullptr,
  };
}

} // namespace lunaredu::lunaros
