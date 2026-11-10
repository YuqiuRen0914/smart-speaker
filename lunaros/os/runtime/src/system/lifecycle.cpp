#include "runtime/system/system.h"

#include "kernel/service_executor.h"
#include "kernel/service.h"
#include <esp_log.h>
#include "bsp/lunaredu_bsp.h"

namespace lunaredu::lunaros {
namespace {

ServiceStartResult start_system_service(void *) {
  const esp_err_t result=bsp_board_init();
  if (result!=ESP_OK) {
    const std::string error=esp_err_to_name(result);
    ESP_LOGE("system.board", "bsp init failed: %s", error.c_str());
    return {ServiceStartDisposition::Failed,
            error.empty() ? "board_initialization_failed" : error};
  }
  return {ServiceStartDisposition::Ready, "system_ready"};
}

} // namespace

ServiceNode system_service_node() {
  return {
      {kKernelReadyServiceId, "System Runtime", kMainServiceExecutor,
       ServiceCriticality::Required, nullptr, 0},
      start_system_service,
  };
}

} // namespace lunaredu::lunaros
