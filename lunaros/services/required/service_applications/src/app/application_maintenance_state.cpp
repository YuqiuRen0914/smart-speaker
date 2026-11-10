#include "application_maintenance_state.h"
#include <esp_timer.h>
#include <algorithm>
namespace lunaredu::lunaros::apps::runtime_detail {
bool ApplicationMaintenanceState::take_request(int64_t now) {
  return requested_.exchange(false) || now >= deadline_.load();
}
bool ApplicationMaintenanceState::take_retry() { return force_.exchange(false); }
void ApplicationMaintenanceState::schedule(int64_t deadline) { deadline_.store(deadline); }
ApplicationCleanupStatus ApplicationMaintenanceState::snapshot() const {
  std::lock_guard lock(status_mutex_); return status_;
}
void ApplicationMaintenanceState::publish(const ApplicationCleanupStatus &status) {
  std::lock_guard lock(status_mutex_); status_ = status;
}
void ApplicationMaintenanceState::request_cleanup() {
  { std::lock_guard lock(status_mutex_); status_.pending = true; status_.detail = "application_cleanup_pending"; }
  schedule(0); poll();
}
bool ApplicationMaintenanceState::wake_bound() const {
  std::lock_guard lock(wake_mutex_); return wake_ != nullptr;
}
bool ApplicationMaintenanceState::bind_wake(void (*wake)(void *), void *context) {
  if (!wake || !context) return false;
  bool installed;
  {
    std::lock_guard lock(wake_mutex_);
    if (wake_ && (wake_ != wake || wake_context_ != context)) return false;
    installed = !wake_; wake_ = wake; wake_context_ = context;
  }
  // Requests made before composition installed the bridge remain observable.
  if (installed && requested_.load()) wake(context);
  return true;
}
void ApplicationMaintenanceState::poll() {
  if (esp_timer_get_time() < deadline_.load() || requested_.exchange(true)) return;
  void (*signal)(void *); void *context;
  { std::lock_guard lock(wake_mutex_); signal = wake_; context = wake_context_; }
  if (signal) signal(context);
}
bool ApplicationMaintenanceState::retry() { force_ = true; request_cleanup(); return true; }
uint32_t ApplicationMaintenanceState::delay_ms() const {
  // A producer can request work while I/O schedules its next cleanup. The
  // pending wake must win over that later deadline, including coalesced wakes.
  if (requested_.load()) return 1;
  const auto remaining = deadline_.load() - esp_timer_get_time();
  if (remaining >= int64_t(UINT32_MAX) * 1000) return UINT32_MAX;
  return remaining <= 0 ? 1 : uint32_t(std::min<int64_t>(UINT32_MAX, (remaining + 999) / 1000));
}
}
