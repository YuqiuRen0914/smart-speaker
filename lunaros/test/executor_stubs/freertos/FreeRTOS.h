#pragma once
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>
using BaseType_t = int;
using UBaseType_t = unsigned;
using TickType_t = uint32_t;
using TaskHandle_t = void *;
inline constexpr int pdTRUE = 1, pdFALSE = 0, pdPASS = 1;
inline constexpr TickType_t portMAX_DELAY = UINT32_MAX;
inline TickType_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
struct TestQueue { size_t depth, item_size; std::deque<std::vector<uint8_t>> data; };
using QueueHandle_t = TestQueue *;
struct StaticSemaphore_t { bool given = false; };
using SemaphoreHandle_t = StaticSemaphore_t *;
inline uintptr_t test_task = 1;
inline uint32_t test_notifications[3]{};
inline size_t test_semaphore_creates = 0;
inline int test_queue_allocations_left = -1;
inline int test_live_queues = 0;
inline QueueHandle_t xQueueCreate(size_t depth, size_t size) {
  if (test_queue_allocations_left == 0) return nullptr;
  if (test_queue_allocations_left > 0) --test_queue_allocations_left;
  ++test_live_queues;
  return new TestQueue{depth, size, {}};
}
inline void vQueueDelete(QueueHandle_t queue) { --test_live_queues; delete queue; }
inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue) {
  return static_cast<UBaseType_t>(queue->data.size());
}
inline BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t) {
  if (queue->data.size() == queue->depth) return pdFALSE;
  const auto *bytes = static_cast<const uint8_t *>(item);
  queue->data.emplace_back(bytes, bytes + queue->item_size);
  return pdTRUE;
}
inline BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t) {
  if (queue->data.empty()) return pdFALSE;
  std::memcpy(item, queue->data.front().data(), queue->item_size);
  queue->data.pop_front();
  return pdTRUE;
}
inline TaskHandle_t xTaskGetCurrentTaskHandle() {
  return reinterpret_cast<TaskHandle_t>(test_task);
}
inline void xTaskNotifyGive(TaskHandle_t task) {
  ++test_notifications[reinterpret_cast<uintptr_t>(task)];
}
inline uint32_t ulTaskNotifyTake(BaseType_t, TickType_t) {
  const auto value = test_notifications[test_task];
  test_notifications[test_task] = 0;
  return value;
}
inline SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *storage) {
  ++test_semaphore_creates;
  return storage;
}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
  semaphore->given = true;
  return pdTRUE;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks);
