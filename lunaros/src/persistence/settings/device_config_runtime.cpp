#include "lunaredu_lunaros/persistence/settings.h"

#include "lunaredu_lunaros/kernel/service_executor.h"
#include "lunaredu_lunaros/kernel/service_host.h"
#include "lunaredu_lunaros/kernel/service_ids.h"

namespace lunaredu::lunaros {
namespace {

ServiceStartResult start_settings_service(void *) {
  return setup_device_config_service()
             ? ServiceStartResult{ServiceStartDisposition::Ready,
                                  "settings_ready"}
             : ServiceStartResult{ServiceStartDisposition::Failed,
                                  "settings_initialization_failed"};
}

} // namespace

ServiceNode settings_service_node() {
  static constexpr ServiceDependency kDependencies[] = {
      {kSystemRuntimeServiceId, true},
  };
  return {
      {kSettingsServiceId, "Persistent Settings", kMainServiceExecutor,
       ServiceCriticality::Required, kDependencies,
       sizeof(kDependencies) / sizeof(kDependencies[0])},
      start_settings_service,
  };
}

} // namespace lunaredu::lunaros
