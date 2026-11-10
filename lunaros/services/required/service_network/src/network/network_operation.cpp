#include "lunaros_network_jobs.h"
#include "lunaros_network_lifecycle.h"
#include "network/network_service_internal.h"
#include "network/network_job_manager_internal.h"
#include "kernel/runtime.h"
#include "kernel/service_executor.h"
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>

namespace lunaredu::lunaros {
bool poll_network_ota_retirement(uint32_t now_ms) {
  auto *runtime = network_service_internal().owner_runtime();
  return runtime && runtime->current_task_owns_executor(kNetworkIoServiceExecutor) &&
      network_maintenance_state() == NetworkMaintenanceState::OtaPending &&
      run_next_network_retirement_job(now_ms);
}
bool execute_network_operation(const NetworkJobDefinition &definition,std::string &error) {
  if(!definition.runner){error="network_operation_missing_runner";return false;}
  // Nested protocol phases already own the declared I/O runner. Never wait
  // on that executor from within its own admitted operation.
  if(network_job_runner_context_active()) {
    if(definition.job_class != NetworkJobClass::Normal ||
       !network_job_runner_accepts_normal_work()) {
      error="network_operation_admission_closed";return false;
    }
    const auto result=definition.runner({},definition.context);
    error=result.detail;return result.succeeded;
  }
  struct Operation {
    NetworkJobDefinition definition;
    NetworkJobRunResult result;
    std::atomic<bool> finished{false};
  } operation{definition,{},false};
  NetworkJobDefinition job=definition;
  job.context=&operation;
  job.runner=[](NetworkJobHandle handle,void *opaque){
    auto &operation=*static_cast<Operation *>(opaque);
    operation.result=operation.definition.runner(handle,operation.definition.context);
    operation.finished.store(true,std::memory_order_release);
    return operation.result;
  };
  job.maximum_attempts=1;job.retain_terminal=false;
  if(!job.deadline_ms)job.deadline_ms=uint32_t(esp_timer_get_time()/1000)+30000;
  NetworkJobHandle handle;
  if(!submit_network_job(job,handle,error))return false;
  auto *runtime=network_service_internal().owner_runtime();
  const bool on_io=runtime&&runtime->current_task_owns_executor(kNetworkIoServiceExecutor);
  bool cancellation_sent=false;
  while(network_job_active(handle)) {
    const auto now=uint32_t(esp_timer_get_time()/1000);
    if(!cancellation_sent&&int32_t(now-job.deadline_ms)>=0){
      std::string ignored;(void)cancel_network_job(handle,ignored);cancellation_sent=true;
    }
    if(on_io)(void)run_next_network_job(now);
    // Cancellation of running work is not ownership return: callbacks still
    // borrow operation and caller buffers until their bounded completion.
    if(network_job_active(handle))vTaskDelay(1);
  }
  if(!operation.finished.load(std::memory_order_acquire)){
    error="network_operation_not_started";return false;
  }
  error=operation.result.detail;return operation.result.succeeded;
}
}
