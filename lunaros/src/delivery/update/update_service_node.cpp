#include "lunaredu_lunaros/delivery/update.h"

#include "lunaredu_lunaros/kernel/service_executor.h"
#include "lunaredu_lunaros/kernel/service_host.h"
#include "lunaredu_lunaros/kernel/service_ids.h"

namespace lunaredu::lunaros {
namespace {

ServiceStartResult start_update_service(void *) {
  setup_ota_service();
  return {ServiceStartDisposition::Ready, "ota_ready"};
}

} // namespace

ServiceNode update_service_node() {
  static constexpr ServiceDependency kDependencies[] = {
      // Application OTA writes the inactive flash slot directly. A broken
      // resource pool must not prevent the recovery update endpoint starting.
      {kSystemRuntimeServiceId, true},
  };
  return {
      {kUpdateServiceId, "Firmware Update", kMainServiceExecutor,
       ServiceCriticality::Required, kDependencies,
       sizeof(kDependencies) / sizeof(kDependencies[0])},
      start_update_service,
  };
}

} // namespace lunaredu::lunaros
