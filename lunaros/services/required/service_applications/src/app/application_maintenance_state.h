#pragma once
#include "app/execution/application_maintenance.h"
#include <atomic>
#include <mutex>
namespace lunaredu::lunaros::apps::runtime_detail {
class ApplicationMaintenanceState {
public:
  bool take_request(int64_t now);
  bool take_retry();
  void schedule(int64_t deadline);
  ApplicationCleanupStatus snapshot() const;
  void publish(const ApplicationCleanupStatus &);
  void request_cleanup();
  void poll();
  bool retry();
  bool bind_wake(void (*)(void *), void *);
  bool wake_bound() const;
  uint32_t delay_ms() const;
  int64_t next_cleanup_us = 0; // Serialized on the catalog I/O owner.
private:
  std::atomic<bool> requested_{false}, force_{false};
  std::atomic<int64_t> deadline_{0};
  mutable std::mutex status_mutex_;
  ApplicationCleanupStatus status_;
  mutable std::mutex wake_mutex_;
  void (*wake_)(void *) = nullptr;
  void *wake_context_ = nullptr;
};
uint32_t retired_application_count();
}
