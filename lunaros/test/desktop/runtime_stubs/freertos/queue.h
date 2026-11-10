#pragma once
#include "FreeRTOS.h"
struct TestQueue { size_t size; bool ready; unsigned char data[128]; };
using QueueHandle_t = TestQueue *;
inline QueueHandle_t xQueueCreate(size_t, size_t size) {
  if (size > 128) std::abort();
  ++runtime_test::sync_allocations;
  return new TestQueue{size, false, {}};
}
inline void vQueueDelete(QueueHandle_t q) { --runtime_test::sync_allocations; delete q; }
inline int xQueueReceive(QueueHandle_t q, void *out, TickType_t) {
  if (!q->ready) throw runtime_test::WorkerIdle{};
  std::memcpy(out, q->data, q->size); q->ready = false; return pdTRUE;
}
inline int xQueueSend(QueueHandle_t q, const void *in, TickType_t) {
  if (runtime_test::reject_queue || q->ready) return pdFALSE;
  std::memcpy(q->data, in, q->size); q->ready = true; return pdTRUE;
}
