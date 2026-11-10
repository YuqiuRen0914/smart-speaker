#include "lunaredu_lunaros/persistence/system_runtime.h"

#include "lunaredu_lunaros/kernel/service_executor.h"
#include "lunaredu_lunaros/kernel/service_host.h"
#include "lunaredu_lunaros/kernel/service_ids.h"
#include "lunaredu_lunaros/kernel/log.h"
#include "persistence/system/board_support.h"

namespace lunaredu::lunaros {
namespace {

ServiceStartResult start_system_service(void *) {
  std::string error;
  if (!board_support_initialize(error)) {
    LOGE("system.board", "bsp init failed: %s", error.c_str());
    return {ServiceStartDisposition::Failed,
            error.empty() ? "board_initialization_failed" : error};
  }
  return {ServiceStartDisposition::Ready, "system_ready"};
}

} // namespace

ServiceNode system_service_node() {
  return {
      {kSystemRuntimeServiceId, "System Runtime", kMainServiceExecutor,
       ServiceCriticality::Required, nullptr, 0},
      start_system_service,
  };
}

} // namespace lunaredu::lunaros
