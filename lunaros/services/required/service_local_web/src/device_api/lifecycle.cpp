#include "lunaros_device_api_service_id.h"
#include "lunaros_network_service_id.h"
#include "lunaros_storage_service_id.h"
#include "lunaros_local_web_lifecycle.h"
#include "device_api/device_api.h"
#include "device_api/device_projection.h"
#include "lunaros_local_web_lifecycle.h"
#include "kernel/service.h"
#include "kernel/service_executor.h"
namespace lunaredu::lunaros {
namespace {
ServiceStartResult start_device_api_service(void *context) {
  auto &registry = *static_cast<DeviceProjectionRegistry *>(context);
  std::string error;
  if (!local_web_start(registry, error)) {
    report_device_host_connection(registry.device(), DeviceHostTransport::LocalWeb, DeviceHostConnectionState::Error);
    return {ServiceStartDisposition::Failed, error};
  }
  return {ServiceStartDisposition::Ready, "device_api_ready"};
}
bool stop_device_api_service(void *context, std::string &error) {
  const bool stopped = local_web_stop(error);
  report_device_host_connection(static_cast<DeviceProjectionRegistry *>(context)->device(), DeviceHostTransport::LocalWeb, stopped ? DeviceHostConnectionState::Disconnected
                                        : DeviceHostConnectionState::Error);
  return stopped;
}
}
ServiceNode device_api_service_node(DeviceProjectionRegistry &registry) {
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
      &registry,
      nullptr,
  };
}

}
