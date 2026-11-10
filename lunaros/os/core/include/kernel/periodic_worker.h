#pragma once
#include "kernel/managed_task.h"
#include <atomic>
namespace lunaredu::lunaros {
struct PeriodicWorkerPorts {
  void *context = nullptr;
  uint32_t (*period_us)(void *) = nullptr;
  void (*step)(void *, uint64_t released_at_us, uint64_t started_at_us) = nullptr;
  void (*exited)(void *) = nullptr;
  // Optional owner initialization, before timer releases or startup ack.
  bool (*initialize)(void *, std::string &error) = nullptr;
  // Owner confirmation after successful timer start, before startup ack.
  bool (*activated)(void *, std::string &error) = nullptr;
};
// OS-owned microsecond release mechanism, usable by any Service. Timer delivery
// only wakes the managed task. It never executes work on the timer owner.
class PeriodicWorker {
public:
  bool start(KernelRuntime &, const ManagedTaskDefinition &, PeriodicWorkerPorts,
             std::string &error);
  // Nonblocking checked retirement. The caller retains this object until true.
  bool stop(std::string &error);
  bool running() const;
private:
  static void run(void *);
  static void notify(void *);
  KernelRuntime *runtime_ = nullptr;
  PeriodicWorkerPorts ports_;
  std::array<char, kManagedTaskIdBytes> id_{};
  std::atomic<ManagedTaskHandle> handle_{nullptr};
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> started_{false};
  std::atomic<uint64_t> released_at_us_{0};
  std::atomic<uint64_t> expected_release_us_{0};
  std::atomic<uint32_t> period_us_{0};
  StaticMutex delivery_mutex_;
  void *timer_ = nullptr;
  std::string startup_error_; // Published by the startup acknowledgement.
  std::atomic<bool> initialization_failed_{false};
};
} // namespace lunaredu::lunaros
