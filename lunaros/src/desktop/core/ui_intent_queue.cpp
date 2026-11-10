#include "lunaredu_lunaros/desktop/core/ui_intent_queue.h"

#include <cstdio>
#include <cstring>
#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#else
#include <atomic>
#endif

namespace lunaredu::desktop {
namespace {
// Queue operations only copy bounded envelopes. Share one private static lock
// across the ingress instances; no heap allocation or platform types in the API.
#ifdef ESP_PLATFORM
StaticSemaphore_t g_lock_storage{};
SemaphoreHandle_t const g_lock = xSemaphoreCreateMutexStatic(&g_lock_storage);
#else
std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
#endif
}
#ifndef ESP_PLATFORM
LunarOSUiIntentQueue::LunarOSUiIntentQueue()
    : intents_(owned_intents_.data()) {}
#endif

LunarOSUiIntentQueue::LunarOSUiIntentQueue(
    std::array<LunarOSUiIntent, kLunarOSUiIntentQueueCapacity> &storage)
    : intents_(storage.data()) {}

void LunarOSUiIntentQueue::lock() {
#ifdef ESP_PLATFORM
  configASSERT(g_lock != nullptr);
  (void)xSemaphoreTake(g_lock, portMAX_DELAY);
#else
  while (g_lock.test_and_set(std::memory_order_acquire)) {
  }
#endif
}

void LunarOSUiIntentQueue::unlock() {
#ifdef ESP_PLATFORM
  (void)xSemaphoreGive(g_lock);
#else
  g_lock.clear(std::memory_order_release);
#endif
}

void LunarOSUiIntentQueue::reset() {
  lock();
  head_ = 0;
  tail_ = 0;
  count_ = 0;
  unlock();
}

bool LunarOSUiIntentQueue::enqueue(const char *type, const char *payload) {
  if (type == nullptr || type[0] == '\0') {
    return false;
  }
  const char *normalized_payload = payload == nullptr ? "{}" : payload;
  if (std::strlen(type) >= kLunarOSUiIntentTypeCapacity ||
      std::strlen(normalized_payload) >= kLunarOSUiIntentPayloadCapacity) {
    return false;
  }

  lock();
  if (count_ >= kLunarOSUiIntentQueueCapacity) {
    unlock();
    return false;
  }
  LunarOSUiIntent &pending = intents_[tail_];
  std::snprintf(pending.type.data(), pending.type.size(), "%s", type);
  std::snprintf(pending.payload.data(), pending.payload.size(), "%s",
                normalized_payload);
  tail_ = (tail_ + 1U) % kLunarOSUiIntentQueueCapacity;
  ++count_;
  unlock();
  return true;
}

bool LunarOSUiIntentQueue::take_one(LunarOSUiIntent &output) {
  lock();
  if (count_ == 0) {
    unlock();
    return false;
  }
  output = intents_[head_];
  head_ = (head_ + 1U) % kLunarOSUiIntentQueueCapacity;
  --count_;
  unlock();
  return true;
}

size_t LunarOSUiIntentQueue::take(
    std::array<LunarOSUiIntent, kLunarOSUiIntentQueueCapacity> &output) {
  lock();
  size_t count = 0;
  while (count < output.size() && count_ > 0) {
    output[count++] = intents_[head_];
    head_ = (head_ + 1U) % kLunarOSUiIntentQueueCapacity;
    --count_;
  }
  unlock();
  return count;
}

} // namespace lunaredu::desktop
