#pragma once
#include <cstddef>
#include <cstring>
#include <deque>
#include <vector>
struct TestQueue { size_t capacity; size_t item_size; std::deque<std::vector<char>> items; };
using QueueHandle_t = TestQueue *;
inline bool test_fail_queue = false;
inline int test_queue_allocations = 0;
inline QueueHandle_t xQueueCreate(size_t count, size_t item_size) {
  if (test_fail_queue) return nullptr;
  ++test_queue_allocations;
  return new TestQueue{count, item_size, {}};
}
inline int xQueueSend(QueueHandle_t q, const void *item, int) {
  if (!q || q->items.size() >= q->capacity) return 0;
  q->items.emplace_back(q->item_size);
  std::memcpy(q->items.back().data(), item, q->item_size);
  return 1;
}
inline int xQueueReceive(QueueHandle_t q, void *item, int) {
  if (!q || q->items.empty()) return 0;
  std::memcpy(item, q->items.front().data(), q->item_size);
  q->items.pop_front();
  return 1;
}
inline void xQueueReset(QueueHandle_t q) { q->items.clear(); }
