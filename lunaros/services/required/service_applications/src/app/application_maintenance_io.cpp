#include "applications_service_internal.h"
#include "app/application_installation.h"
#include "lunaros_storage.h"
#include "kernel/runtime.h"
#include <esp_timer.h>
#include <limits>
namespace lunaredu::lunaros::apps {
namespace runtime_detail {
namespace {
RetiredApplicationOwner *retired = nullptr;
RetiredApplicationOwner *retired_tail = nullptr;
RetiredApplicationOwner *retiring_current = nullptr;
std::atomic<uint32_t> retired_count{0};
// Queue bookkeeping never shares the filesystem transaction gate. Main stop
// checks and failed-owner adoption must remain usable while another ID commits.
std::mutex retired_mutex;
}
void adopt_retired_application(RetiredApplicationOwner &owner) {
  {
    std::lock_guard lock(retired_mutex);
    owner.next = nullptr;
    if (retired_tail) retired_tail->next = &owner;
    else retired = &owner;
    retired_tail = &owner; ++retired_count;
  }
}
uint32_t retired_application_count() { return retired_count.load(); }
bool has_retired_applications(KernelRuntime *runtime) {
  std::lock_guard lock(retired_mutex);
  if (retiring_current && retiring_current->runtime == runtime) return true;
  for (auto *owner = retired; owner; owner = owner->next)
    if (owner->runtime == runtime) return true;
  return false;
}
bool poll_retired_applications() {
  RetiredApplicationOwner *owner;
  {
    std::lock_guard lock(retired_mutex);
    // Main shutdown may observe retirement while the I/O owner is already
    // checking a callback. Keep that in-flight owner visible until it returns.
    if (retiring_current) return true;
    owner = retired;
    if (!owner) return false;
    retiring_current = owner;
    retired = owner->next;
    if (!retired) retired_tail = nullptr;
  }
  bool stopped = owner->stop(owner->context); // Nonblocking stop handshake.
  if(stopped){
    std::unique_lock lock(catalog_mutex,std::try_to_lock);
    // VM acknowledgement alone does not release its cross-channel identity.
    // Keep the context until that release can also be acknowledged, with no
    // wait for a different package's storage transaction on Main.
    if(lock.owns_lock())active_owners.erase(owner->context);
    else stopped=false;
  }
  bool more;
  {
    std::lock_guard lock(retired_mutex);
    retiring_current = nullptr;
    if (stopped) { --retired_count; }
    else {
      owner->next = nullptr;
      if (retired_tail) retired_tail->next = owner;
      else retired = owner;
      retired_tail = owner;
    }
    more = retired_count != 0;
  }
  if (stopped) owner->destroy(owner->context);
  return more;
}
}
uint32_t ApplicationsService::maintenance_delay_ms() const {
  const auto phase = impl_->phase.load();
  return phase == Impl::Phase::Ready || phase == Impl::Phase::Degraded
      ? impl_->maintenance.delay_ms() : UINT32_MAX;
}
void ApplicationsService::run_maintenance_io() {
  std::string error;
  Impl::Admission admission(*impl_, error, true);
  if (!admission) return;
  using namespace runtime_detail;
  const auto now = esp_timer_get_time();
  if (!impl_->maintenance.take_request(now)) return;
  run_application_work_io();
  const bool retiring = poll_retired_applications();
  const auto normal_next = retiring ? now + 100'000 : now + 30'000'000;
  impl_->maintenance.schedule(normal_next);
  if (!storage_ready()) return;
  std::unique_lock lock(catalog_mutex, std::try_to_lock);
  if (!lock.owns_lock()) { impl_->maintenance.schedule(now + 100'000); return; }
  auto status = impl_->maintenance.snapshot();
  const bool force = impl_->maintenance.take_retry();
  if (!status.pending && !force) {
    if (!retiring) impl_->maintenance.schedule(std::numeric_limits<int64_t>::max());
    return;
  }
  if (!active_owners.empty() || (!force && now < impl_->maintenance.next_cleanup_us)) return;
  status.running = true; ++status.attempts; impl_->maintenance.publish(status);
  std::vector<Application> catalog;
  const bool valid = read_catalog(catalog);
  const bool ok = valid && collect_unreferenced_application_packages(catalog);
  status.running = false; status.pending = !ok;
  status.detail = ok ? "" : valid ? "application_cleanup_failed" : "installed_catalog_invalid";
  impl_->maintenance.publish(status);
  impl_->maintenance.next_cleanup_us = now + 30'000'000;
  if (ok && !retiring) impl_->maintenance.schedule(std::numeric_limits<int64_t>::max());
}
}

namespace lunaredu::lunaros::apps {
ApplicationCleanupStatus ApplicationsService::cleanup_status() const {
  auto status = impl_->maintenance.snapshot();
  // Orphans share one physical catalog; this count is a shared storage fact.
  status.retiring_owners = runtime_detail::retired_application_count();
  return status;
}
bool ApplicationsService::retry_cleanup() {
  std::string error; Impl::Admission admission(*impl_, error, true);
  return admission && impl_->maintenance.retry();
}
void ApplicationsService::poll_maintenance() {
  // Hardware retirement callbacks require Main even when storage I/O is stalled
  // or the service is stopping. The queue handshake never waits for storage.
  if (impl_->runtime && impl_->runtime->current_task_owns_executor(kMainServiceExecutor))
    runtime_detail::poll_retired_applications();
  const auto phase = impl_->phase.load();
  if (phase != Impl::Phase::Ready && phase != Impl::Phase::Degraded) return;
  if (runtime_detail::retired_application_count() && impl_->maintenance.delay_ms() > 100) impl_->maintenance.schedule(0);
  impl_->maintenance.poll();
}
bool ApplicationsService::bind_maintenance_wake(void (*wake)(void *), void *context) {
  return impl_->maintenance.bind_wake(wake, context);
}
bool ApplicationsService::bind_maintenance_worker(const ApplicationMaintenanceWorkerPorts &ports) {
  if(!ports.enable||!ports.retired)return false;
  auto &s=*impl_;
  const auto &bound=s.maintenance_worker;
  if(bound.enable)return bound.context==ports.context&&bound.enable==ports.enable&&bound.retired==ports.retired;
  const auto phase=s.phase.load();
  if(phase==Impl::Phase::Stopping||phase==Impl::Phase::Stopped)return false;
  s.maintenance_worker=ports;return true;
}
}
