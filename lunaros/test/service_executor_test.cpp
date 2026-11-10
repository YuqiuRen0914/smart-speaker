// Exercise the production queue pump with a deterministic RTOS shim. Including
// this unit permits a producer to enqueue while a callback is being executed.
#include "lunaredu_lunaros/kernel/service_ids.h"
#include "../src/kernel/execution/service_executor.cpp"
#include <cassert>
#include <iostream>

using namespace lunaredu::lunaros;

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) {
  assert(ticks == portMAX_DELAY);
  const auto caller = test_task;
  test_task = 1;
  assert(pump_service_executor(kMainServiceExecutor, 0));
  test_task = caller;
  return semaphore->given ? pdTRUE : pdFALSE;
}

namespace {
size_t replenished = 0;
ServiceOperationResult queued_result;
void replenish(void *, ServiceOperationResult &) {
  if (++replenished < 5) {
    ServiceInvocation next{replenish, nullptr, &queued_result, nullptr};
    assert(xQueueSend(find_executor(kMainServiceExecutor)->queue, &next, 0));
    xTaskNotifyGive(reinterpret_cast<TaskHandle_t>(1));
  }
}
void increment(void *context, ServiceOperationResult &) {
  assert(test_task == 1);
  ++*static_cast<int *>(context);
}
}

int main() {
  std::string error;
  assert(!setup_service_executors({{"same", "a", 1}, {"same", "b", 1}}, &error));
  assert(test_live_queues == 0 && g_executor_count == 0);
  test_queue_allocations_left = 1;
  assert(!setup_service_executors({{"a", "a", 1}, {"b", "b", 1}}, &error));
  assert(test_live_queues == 0 && g_executor_count == 0);
  test_queue_allocations_left = -1;
  assert(setup_service_executors({{kMainServiceExecutor, "test.main", 8},
      {kNetworkServiceExecutor, "test.network", 8}}, &error));
  assert(g_executor_count == 2 && test_live_queues == 2);
  assert(!find_executor(kNetworkIoServiceExecutor));
  assert(setup_service_executors({{kMainServiceExecutor, "test.main", 8},
      {kNetworkServiceExecutor, "test.network", 8}}, &error));
  assert(test_live_queues == 2);
  assert(!setup_service_executors({{kMainServiceExecutor, "different", 8}}, &error));
  assert(test_live_queues == 2);
  assert(bind_service_executor_to_current_task(kMainServiceExecutor, &error));
  assert(current_task_owns_service_executor());
  int count = 0;
  assert(invoke_service_operation(kMainServiceExecutor, increment, &count, error));
  assert(count == 1 && test_semaphore_creates == 0);
  test_task = 2;
  assert(!current_task_owns_service_executor());
  test_notifications[2] = 7;
  assert(invoke_service_operation(kMainServiceExecutor, increment, &count, error));
  assert(count == 2 && test_notifications[2] == 7);
  assert(!pump_service_executor(kMainServiceExecutor, 0, &error));
  assert(error == "service_executor_wrong_owner_task");
  assert(!invoke_service_operation(kNetworkServiceExecutor, increment, &count, error));
  assert(error == "service_executor_unbound");
  test_task = 1;
  ServiceInvocation first{replenish, nullptr, &queued_result, nullptr};
  assert(xQueueSend(find_executor(kMainServiceExecutor)->queue, &first, 0));
  assert(!unbind_service_executor_from_current_task(kMainServiceExecutor, &error));
  for (size_t round = 1; round <= 5; ++round) {
    assert(pump_service_executor(kMainServiceExecutor, 0));
    assert(replenished == round); // New arrivals cannot extend this round.
  }
  assert(uxQueueMessagesWaiting(find_executor(kMainServiceExecutor)->queue) == 0);
  test_task = 2;
  assert(!unbind_service_executor_from_current_task(kMainServiceExecutor, &error));
  test_task = 1;
  assert(unbind_service_executor_from_current_task(kMainServiceExecutor, &error));
  assert(!current_task_owns_service_executor());
  assert(!invoke_service_operation(kMainServiceExecutor, increment, &count, error));
  assert(error == "service_executor_unbound");
  assert(bind_service_executor_to_current_task(kMainServiceExecutor, &error));
  for (uint8_t n = 0; n < g_executor_count; ++n) vQueueDelete(g_executors[n].queue);
  std::cout << "Executor ownership, bounded pump and notification isolation passed\n";
}
