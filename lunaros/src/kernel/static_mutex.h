#pragma once

#include <atomic>

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

namespace lunaredu::lunaros {

// Allocation-free mutex used by small LunarOS registries. ESP32 uses a static
// FreeRTOS mutex with priority inheritance; host policy tests use atomic_flag.
class StaticMutex {
public:
  StaticMutex() {
#ifdef ESP_PLATFORM
    handle_ = xSemaphoreCreateMutexStatic(&storage_);
#endif
  }

  StaticMutex(const StaticMutex &) = delete;
  StaticMutex &operator=(const StaticMutex &) = delete;

private:
  friend class StaticLockGuard;

#ifdef ESP_PLATFORM
  StaticSemaphore_t storage_{};
  SemaphoreHandle_t handle_ = nullptr;
#else
  std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
#endif
};

class StaticLockGuard {
public:
  explicit StaticLockGuard(StaticMutex &mutex) : mutex_(mutex) {
#ifdef ESP_PLATFORM
    if (mutex_.handle_ != nullptr) {
      (void)xSemaphoreTake(mutex_.handle_, portMAX_DELAY);
    }
#else
    while (mutex_.flag_.test_and_set(std::memory_order_acquire)) {
    }
#endif
  }

  ~StaticLockGuard() {
#ifdef ESP_PLATFORM
    if (mutex_.handle_ != nullptr) {
      (void)xSemaphoreGive(mutex_.handle_);
    }
#else
    mutex_.flag_.clear(std::memory_order_release);
#endif
  }

  StaticLockGuard(const StaticLockGuard &) = delete;
  StaticLockGuard &operator=(const StaticLockGuard &) = delete;

private:
  StaticMutex &mutex_;
};

} // namespace lunaredu::lunaros
