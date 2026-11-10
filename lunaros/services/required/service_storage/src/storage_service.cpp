#include "lunaros_storage_service_id.h"
#include "model/device_model.h"
#include "lunaros_storage_device_operations.h"
#include <esp_log.h>
#include "kernel/service_executor.h"
#include "kernel/service.h"
#include "lunaros_storage.h"
#include "littlefs_store_internal.h"

namespace lunaredu::lunaros {

namespace {

const DeviceCapabilityDescriptor kLittleFsCapability = []() {
  DeviceCapabilityDescriptor descriptor;
  descriptor.id = "littlefs_service";
  return descriptor;
}();

void sync_storage_capability(DeviceCapabilityRegistry &capabilities, bool ready) {
  const bool updated = ready
                           ? capabilities.activate(
                                 storage_service_capability_descriptor())
                           : capabilities.deactivate(
                                 storage_service_capability_descriptor().id);
  if (!updated && ready) {
    ESP_LOGW("system.storage", "failed to register capability id=%s",
         kLittleFsCapability.id);
  }
}

} // namespace

const DeviceCapabilityDescriptor &storage_service_capability_descriptor() {
  return kLittleFsCapability;
}

namespace {

ServiceStartResult start_storage_service(void *context) {
  const bool ready = setup_littlefs_store();
  sync_storage_capability(*static_cast<DeviceCapabilityRegistry *>(context), ready);
  return ready
             ? ServiceStartResult{ServiceStartDisposition::Ready,
                                  "filesystem_ready"}
             : ServiceStartResult{ServiceStartDisposition::Failed,
                                  storage_last_error()};
}

} // namespace

ServiceNode storage_service_node(DeviceCapabilityRegistry &capabilities) {
  static constexpr ServiceDependency kDependencies[] = {
      {kKernelReadyServiceId, true},
  };
  return {
      {kStorageServiceId, "Persistent Storage", kMainServiceExecutor,
       ServiceCriticality::Optional, kDependencies,
       sizeof(kDependencies) / sizeof(kDependencies[0])},
      start_storage_service, nullptr, &capabilities,
  };
}

bool storage_format(DeviceCapabilityRegistry &capabilities) {
  const bool ready = littlefs_store_format();
  sync_storage_capability(capabilities, ready);
  return ready;
}

} // namespace lunaredu::lunaros
