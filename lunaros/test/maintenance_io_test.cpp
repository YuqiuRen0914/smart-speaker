#include <cassert>
#include <iostream>
#include "lunaredu_lunaros/app/maintenance.h"
#include "lunaredu_lunaros/kernel/managed_task.h"
#include <freertos/task.h>

using namespace lunaredu::lunaros;
namespace {
uint32_t clock_ms = 0;
ManagedTaskFunction pending = nullptr;
void *pending_context = nullptr;
bool creation_allowed = true, dispatch_allowed = true, binding_allowed = false;
int creates = 0, returned_tasks = 0;
struct TaskReachedLoop {};
}
int64_t esp_timer_get_time() { return int64_t(clock_ms) * 1000; }
TaskHandle_t xTaskGetCurrentTaskHandle() { return reinterpret_cast<void *>(1); }
void xTaskNotifyGive(TaskHandle_t) {}
uint32_t ulTaskNotifyTake(int, TickType_t) { throw TaskReachedLoop{}; }
void vTaskDelay(TickType_t ticks) {
  clock_ms += ticks;
  if (pending && dispatch_allowed) {
    const auto function = pending;
    pending = nullptr;
    try { function(pending_context); ++returned_tasks; }
    catch (const TaskReachedLoop &) {}
  }
}
namespace lunaredu::lunaros {
bool create_managed_task(const ManagedTaskDefinition &, ManagedTaskFunction function,
                         void *context, ManagedTaskHandle *handle, std::string &error) {
  ++creates;
  assert(handle == nullptr); // startup does not retain a potentially stale task handle
  if (!creation_allowed) { error = "injected_creation_failure"; return false; }
  assert(pending == nullptr);
  pending = function; pending_context = context;
  return true;
}
bool bind_service_executor_to_current_task(const char *, std::string *) { return binding_allowed; }
bool pump_service_executor(const char *, uint32_t, std::string *) { return true; }
void set_network_job_wake_handler(NetworkJobWakeHandler, void *) {}
bool run_next_network_job(uint32_t) { return false; }
uint32_t next_network_job_wake_delay_ms(uint32_t) { return 100; }
}
int main() {
  std::string error;
  creation_allowed = false;
  assert(!start_maintenance_io(error) && error == "injected_creation_failure");
  creation_allowed = true;
  assert(!start_maintenance_io(error) && error == "maintenance_io_bind_failed");
  assert(returned_tasks == 1); // normal return reaches the managed-task cleanup wrapper
  dispatch_allowed = false;
  assert(!start_maintenance_io(error) && error == "maintenance_io_timeout");
  const int attempts = creates;
  assert(!start_maintenance_io(error) && error == "maintenance_io_timeout");
  assert(creates == attempts); // timeout retry joins the existing attempt
  dispatch_allowed = true;
  assert(!start_maintenance_io(error) && error == "maintenance_io_bind_failed");
  assert(returned_tasks == 2);
  binding_allowed = true;
  assert(start_maintenance_io(error));
  assert(error.empty());
  const int ready_attempts = creates;
  assert(start_maintenance_io(error));
  assert(creates == ready_attempts);
  std::cout << "Maintenance task creation/binding failure, timeout join and retry passed\n";
}
