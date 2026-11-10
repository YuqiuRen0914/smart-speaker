#include "lunaros_update_service_id.h"
#include "model/device_model.h"
#include "lunaros_ota_lifecycle.h"
#include "lunaredu_ota.h"
#include "espnow_ota.h"
#include "lunaros_network_service_id.h"
#if LUNAROS_ESPNOW_OTA_STORAGE
#include "lunaros_storage_service_id.h"
#endif

#include "kernel/service_executor.h"
#include "kernel/service.h"

namespace lunaredu::lunaros {
namespace {

ServiceStopResult stop_update_service(void *) {
  std::string error;
  if(!internal::stop_espnow_ota(error))return {ServiceStopDisposition::Pending,error};
  return {ServiceStopDisposition::Stopped,"ota_stopped"};
}

ServiceStartResult start_update_service(void *context) {
  if(!internal::bind_espnow_ota())return {ServiceStartDisposition::Failed,"espnow_ota_provider_conflict"};
  setup_ota_service();
  std::string error;
  if (!static_cast<DeviceCapabilityRegistry *>(context)->activate(ota_capability_descriptor(), &error)) {
    std::string cleanup;(void)internal::stop_espnow_ota(cleanup);
    return {ServiceStartDisposition::Failed, error};
  }
  return {ServiceStartDisposition::Ready, "ota_ready"};
}

} // namespace

ServiceNode update_service_node(DeviceCapabilityRegistry &capabilities) {
  static constexpr ServiceDependency kDependencies[] = {
      // Application OTA writes the inactive flash slot directly. A broken
      // resource pool must not prevent the recovery update endpoint starting.
      {kKernelReadyServiceId, true},
#if CONFIG_LUNAREDU_NETWORK_ENABLE_ESPNOW && CONFIG_LUNAROS_NETWORK_ESPNOW_MANAGEMENT
      // Optional for boot readiness, explicit for teardown order. Native
      // staging cannot release its lease after these owners have stopped.
      {kNetworkRuntimeServiceId, false},
#if LUNAROS_ESPNOW_OTA_STORAGE
      {kStorageServiceId, false},
#endif
#endif
  };
  ServiceNode node{
      {kUpdateServiceId, "Firmware Update", kMainServiceExecutor,
       ServiceCriticality::Required, kDependencies,
       sizeof(kDependencies) / sizeof(kDependencies[0])},
      start_update_service, nullptr, &capabilities,
  };
  node.stop_step=stop_update_service;
  return node;
}

} // namespace lunaredu::lunaros
