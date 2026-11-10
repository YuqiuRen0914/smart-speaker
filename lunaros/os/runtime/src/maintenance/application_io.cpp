#include "maintenance/application_io.h"
#include "lunaros_applications.h"
#include "lunaros_network_jobs.h"
#include "kernel/bounded_work.h"
#include "kernel/managed_task.h"
#include "kernel/runtime.h"
#include "kernel/task_signal.h"
#include <algorithm>
#include <mutex>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace lunaredu::lunaros {
namespace {
constexpr const char *kOwner = "lunaros.application_io";
constexpr const char *kTask = "application_io";
constexpr uint32_t kIdleMs = 5000, kRetryMs = 1000;
enum class State { Idle, Starting, Ready, Retiring, Failed };
// Boot-lived wake target; no callback borrows a worker's stack or a stale handle.
struct Bridge {
  std::mutex mutex;
  KernelRuntime *runtime = nullptr;
  apps::ApplicationsService *applications = nullptr;
  TaskHandle_t worker = nullptr;
  State state = State::Idle;
  uint32_t heartbeat = 0, wait_ms = 0, transition = 0, retry_at = 0, deadline = 0;
  bool waiting = false, bounded = false, retry_pending = false, enabled = true, configured = false;
} bridge;
uint32_t now_ms() { return uint32_t(esp_timer_get_time() / 1000); }
void wake(void *opaque) {
  auto &owner = *static_cast<Bridge *>(opaque);
  std::lock_guard lock(owner.mutex);
  if (owner.worker) task_signal::notify(owner.worker);
  // Main's ordinary upkeep observes the service deadline and admits a task.
}
void enable(void *opaque,bool enabled) {
  auto &owner=*static_cast<Bridge *>(opaque);
  std::lock_guard lock(owner.mutex);
  owner.enabled=enabled;
  if(owner.worker)task_signal::notify(owner.worker);
}
bool retired(void *opaque) {
  auto &owner=*static_cast<Bridge *>(opaque);
  std::lock_guard lock(owner.mutex);
  return owner.runtime&&!owner.runtime->task_active(kTask,ManagedTaskOwnerKind::Subsystem,kOwner);
}
void application_io_task(void *) {
  BoundedWorkObserver observer([](void *, bool active, uint32_t deadline) {
    std::lock_guard lock(bridge.mutex);
    bridge.bounded = active; bridge.deadline = deadline;
    if (!active) bridge.heartbeat = now_ms();
  }, nullptr);
  {
    std::lock_guard lock(bridge.mutex);
    bridge.worker = xTaskGetCurrentTaskHandle();
    bridge.state = State::Ready;
    bridge.heartbeat = now_ms();
  }
  auto last_work = now_ms();
  for (;;) {
    {
      std::lock_guard lock(bridge.mutex);
      if(!bridge.enabled){
        bridge.worker=nullptr;bridge.waiting=false;bridge.transition=now_ms();bridge.state=State::Retiring;
        return;
      }
    }
    auto delay = bridge.applications->maintenance_delay_ms();
    bool blocked = false;
    if (delay <= 1) {
      NetworkExternalWork permit;
      std::string error;
      if (acquire_network_external_work(permit, error)) {
        // The permit spans the physical callback, including its final storage
        // release. Never release it merely because OTA has requested a stop.
        {
          BoundedWorkScope scope(now_ms(), 30'000);
          bridge.applications->run_maintenance_io();
        }
        if (!release_network_external_work(permit, error)) {
          // An ownership mismatch is fatal to this lane; retain the accounting
          // and make health fail instead of allowing conflicting flash writes.
          std::lock_guard lock(bridge.mutex);
          bridge.worker = nullptr; bridge.state = State::Failed;
          bridge.retry_pending = false;
          return;
        }
        last_work = now_ms();
        delay = bridge.applications->maintenance_delay_ms();
      } else blocked = true;
    }
    const auto idle = now_ms() - last_work;
    if (idle >= kIdleMs && (delay > kIdleMs || blocked)) {
      std::lock_guard lock(bridge.mutex);
      bridge.worker = nullptr; bridge.waiting = false;
      bridge.transition = now_ms(); bridge.state = State::Retiring;
      return; // Managed wrapper, not this function, releases task admission.
    }
    delay = blocked ? 50 : std::max<uint32_t>(1, delay);
    delay = std::min(delay, idle < kIdleMs ? kIdleMs - idle : 100u);
    {
      std::lock_guard lock(bridge.mutex);
      bridge.heartbeat = now_ms(); bridge.wait_ms = delay; bridge.waiting = true;
    }
    task_signal::wait_ms(delay);
    {
      std::lock_guard lock(bridge.mutex);
      bridge.heartbeat = now_ms(); bridge.waiting = false;
    }
  }
}
}
bool configure_application_io(KernelRuntime &runtime, apps::ApplicationsService &applications, std::string &error) {
  {
    std::lock_guard lock(bridge.mutex);
    if (bridge.runtime && (bridge.runtime != &runtime || bridge.applications != &applications)) {
      error = "application_io_owner_mismatch"; return false;
    }
    if (!runtime.declare_task_owner(ManagedTaskOwnerKind::Subsystem, kOwner, error)) return false;
    bridge.runtime = &runtime; bridge.applications = &applications;
  }
  if (!applications.bind_maintenance_wake(wake, &bridge)) {
    error = "application_maintenance_wake_owner_mismatch"; return false;
  }
  if(!applications.bind_maintenance_worker({&bridge,enable,retired})){
    error="application_maintenance_worker_owner_mismatch";return false;
  }
  {std::lock_guard lock(bridge.mutex);bridge.configured=true;}
  error.clear(); return true;
}
void poll_application_io(KernelRuntime &runtime) {
  if (!runtime.current_task_owns_executor(kMainServiceExecutor)) return;
  {
    std::lock_guard lock(bridge.mutex);
    if (bridge.runtime != &runtime || !bridge.configured || !bridge.enabled || bridge.state == State::Ready || bridge.state == State::Starting) return;
    if (bridge.state == State::Failed && !bridge.retry_pending) return;
    if (bridge.retry_pending && int32_t(now_ms() - bridge.retry_at) < 0) return;
    if (bridge.applications->maintenance_delay_ms() > 1 ||
        network_maintenance_state() != NetworkMaintenanceState::Normal) return;
    if (runtime.task_active(kTask, ManagedTaskOwnerKind::Subsystem, kOwner)) return;
    bridge.state = State::Starting; bridge.transition = now_ms(); bridge.retry_pending = false;
  }
  // Finite extra SRAM only while application filesystem/validation work exists.
  // It never binds or pumps netio, and cannot start a network job runner.
  ManagedTaskDefinition definition{.id = kTask, .owner_service_id = kOwner,
      .role = "application_storage", .stack_bytes = 12288, .priority = 1,
      .core_affinity = 0, .lifetime = ManagedTaskLifetime::Feature,
      .memory = ManagedTaskMemory::Internal};
  definition.owner_kind = ManagedTaskOwnerKind::Subsystem;
  std::string error;
  if (!create_managed_task(runtime, definition, application_io_task, &runtime, nullptr, error)) {
    std::lock_guard lock(bridge.mutex);
    bridge.state = State::Failed; bridge.retry_pending = true; bridge.retry_at = now_ms() + kRetryMs;
  }
}
bool application_io_healthy(const KernelRuntime &runtime) {
  std::lock_guard lock(bridge.mutex);
  if (bridge.runtime != &runtime || !bridge.configured) return false;
  if(!bridge.enabled&&!runtime.task_active(kTask,ManagedTaskOwnerKind::Subsystem,kOwner))return true;
  const auto now = now_ms();
  const bool paused = network_maintenance_state() != NetworkMaintenanceState::Normal;
  if (bridge.state == State::Idle) return paused || bridge.applications->maintenance_delay_ms() > 1;
  if (bridge.state == State::Retiring || bridge.state == State::Starting)
    return now - bridge.transition < kIdleMs ||
        (bridge.state == State::Retiring && (paused || bridge.applications->maintenance_delay_ms() > 1));
  if (bridge.state != State::Ready) return false;
  if (bridge.bounded) return int32_t(now - bridge.deadline) < 1000;
  const auto elapsed = now - bridge.heartbeat;
  return elapsed < 1000 || (bridge.waiting && (elapsed <= bridge.wait_ms || elapsed - bridge.wait_ms < 1000));
}
}
