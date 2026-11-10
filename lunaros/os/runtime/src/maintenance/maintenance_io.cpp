#include "lunaros_network_service_id.h"
#include "diagnostics/function_probe.h"
#include "kernel/task_signal.h"
#include "kernel/bounded_work.h"
#include "runtime/product_runtime.h"
#include "maintenance/maintenance.h"
#include "maintenance/application_io.h"
#include "kernel/managed_task.h"
#include "kernel/runtime.h"
#include "lunaros_network_lifecycle.h"
#include "lunaros_network_jobs.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <esp_timer.h>
#ifdef ESP_PLATFORM
#include <esp_log.h>
#endif
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "sdkconfig.h"

namespace lunaredu::lunaros {
namespace {
constexpr const char *kMaintenanceOwner = "lunaros.maintenance";
enum class IoStartState { Idle, Starting, Ready, Retiring, Failed };
#if defined(LUNAROS_DIAGNOSTIC_PROBES)
std::atomic<uint32_t> diagnostic_phase{0},diagnostic_phase_started{0};
#endif
struct IoDiagnosticScope {
  system::probe::Scope scope;
#if defined(LUNAROS_DIAGNOSTIC_PROBES)
  bool selected;
  explicit IoDiagnosticScope(system::probe::Point point):scope(point),selected((system::probe::selected()&128)!=0){
    if(selected){diagnostic_phase_started.store(system::probe::wall_clock());diagnostic_phase.store(unsigned(point));}
  }
  ~IoDiagnosticScope(){if(selected)diagnostic_phase.store(0);}
#else
  explicit IoDiagnosticScope(system::probe::Point point):scope(point){}
#endif
};
template<class F> decltype(auto) measure_io(system::probe::Point point,F &&work){IoDiagnosticScope scope(point);return work();}
// The callbacks borrow this boot-lifetime bridge, never a retired task handle.
// Notification and handle withdrawal share the same lock. No callback is run
// under a NetworkJobManager lock and worker creation is outside this lock.
struct IoBridge {
  std::mutex mutex;
  KernelRuntime *runtime = nullptr;
  apps::ApplicationsService *applications = nullptr;
  TaskHandle_t worker = nullptr;
  bool requested = false;
  std::atomic<IoStartState> state{IoStartState::Idle};
  std::atomic<uint32_t> heartbeat{0}, wait_ms{0};
  std::atomic<bool> waiting{false}, ever_ready{false};
  uint32_t retry_at = 0;
  bool retry_pending = false;
  bool bounded_work = false;
  uint32_t work_deadline = 0;
  std::atomic<uint32_t> transition_started{0};
} bridge;
constexpr uint32_t kIdleGraceMs = 5000;
constexpr uint32_t kRetryDelayMs = 1000;
constexpr uint32_t kWorkerStartTimeoutMs = 5000;
#ifdef CONFIG_LUNAREDU_LUNAROS_MAINTENANCE_IO_CORE
constexpr int32_t kMaintenanceIoCore = CONFIG_LUNAREDU_LUNAROS_MAINTENANCE_IO_CORE;
static_assert(kMaintenanceIoCore < CONFIG_FREERTOS_NUMBER_OF_CORES);
#else
constexpr int32_t kMaintenanceIoCore = 0;
#endif
#ifdef CONFIG_LUNAREDU_LUNAROS_MAINTENANCE_IO_PRIORITY
constexpr uint32_t kMaintenanceIoPriority = CONFIG_LUNAREDU_LUNAROS_MAINTENANCE_IO_PRIORITY;
#else
constexpr uint32_t kMaintenanceIoPriority = 1;
#endif
uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
uint32_t work_delay() {
  return next_network_job_wake_delay_ms(now_ms());
}
void request_owner(void *opaque, bool executor_activation) {
  auto &owner = *static_cast<IoBridge *>(opaque);
  KernelRuntime *runtime;
  bool on_composition;
  {
    std::lock_guard lock(owner.mutex);
    // A queue mutation is only a deadline notification. Future retries and
    // cancelled jobs do not require an internal-stack worker. Executor callers
    // need binding before they can enqueue, so preserve that explicit demand.
    if (executor_activation) owner.requested = true;
    runtime = owner.runtime;
    on_composition = runtime && runtime->current_task_owns_executor(kMainServiceExecutor);
    if (owner.worker) task_signal::notify(owner.worker);
  }
  // A Main caller can wait for executor binding without deadlocking its own
  // future poll. Foreign callers only signal; the composition poll admits work.
  if (runtime && on_composition) poll_maintenance_io(*runtime);
}
void request_work(void *opaque) { request_owner(opaque, false); }
void request_executor(void *opaque) { request_owner(opaque, true); }
void io_task(void *opaque) {
  auto &runtime = *static_cast<KernelRuntime *>(opaque);
  BoundedWorkObserver bounded_work([](void *, bool active, uint32_t deadline) {
    std::lock_guard lock(bridge.mutex);
    bridge.bounded_work = active;
    bridge.work_deadline = deadline;
    if (!active) bridge.heartbeat.store(now_ms());
  }, nullptr);
  std::string error;
  if (!runtime.bind_executor(kNetworkIoServiceExecutor, &error)) {
    std::lock_guard lock(bridge.mutex);
    bridge.retry_pending = true;
    bridge.retry_at = now_ms() + kRetryDelayMs;
    bridge.state.store(IoStartState::Failed, std::memory_order_release);
    return;
  }
  {
    std::lock_guard lock(bridge.mutex);
    bridge.worker = xTaskGetCurrentTaskHandle();
    bridge.heartbeat.store(now_ms());
    bridge.ever_ready.store(true);
    bridge.state.store(IoStartState::Ready, std::memory_order_release);
  }
  uint32_t last_work = now_ms();
  for (;;) {
    system::probe::Scope diagnostic_loop(system::probe::Point::IoLoop);
    bool signalled;
    {
      std::lock_guard lock(bridge.mutex);
      signalled = bridge.requested;
      bridge.requested = false;
    }
    const bool invoked = runtime.executor_pending_calls(kNetworkIoServiceExecutor) != 0;
    measure_io(system::probe::Point::IoPump,[&]{runtime.pump_executor(kNetworkIoServiceExecutor, 0);});
    const bool ran_job = measure_io(system::probe::Point::IoJob,[&]{return run_next_network_job(now_ms());});
    if (signalled || invoked || ran_job) last_work = now_ms();
    auto next_delay = measure_io(system::probe::Point::IoDelay,[]{return work_delay();});
    const auto idle_ms = now_ms() - last_work;
    // A real admitted near-future deadline can reuse the existing owner. Do
    // not retire its internal stack just before the same queued job wakes it.
    // Distant deadlines/no work still return SRAM after the ordinary grace.
    if (idle_ms >= kIdleGraceMs && next_delay > kIdleGraceMs) {
      IoDiagnosticScope diagnostic_retire(system::probe::Point::IoRetire);
      std::lock_guard lock(bridge.mutex);
      // unbind atomically closes executor admission; a newly copied wake then
      // records demand against this bridge instead of notifying deleted storage.
      if (!bridge.requested && work_delay() > kIdleGraceMs &&
          runtime.unbind_executor(kNetworkIoServiceExecutor, &error)) {
        bridge.worker = nullptr;
        bridge.waiting.store(false);
        bridge.transition_started.store(now_ms());
        bridge.state.store(IoStartState::Retiring, std::memory_order_release);
        return; // Managed wrapper releases admission and performs SDK deletion.
      }
    }
    // A transient failed retirement must not turn an otherwise idle worker into
    // a 1 ms poller. Work notifications still interrupt this bounded retry wait.
    const uint32_t until_idle = idle_ms < kIdleGraceMs ? kIdleGraceMs - idle_ms :
      (next_delay<=kIdleGraceMs?next_delay:100);
    next_delay = std::max<uint32_t>(1, std::min(next_delay, until_idle));
    {
      std::lock_guard lock(bridge.mutex);
      bridge.heartbeat.store(now_ms());
      bridge.wait_ms.store(next_delay);
      bridge.waiting.store(true, std::memory_order_release);
    }
    measure_io(system::probe::Point::IoWait,[&]{task_signal::wait_ms(std::max<uint32_t>(1,next_delay));});
    {
      std::lock_guard lock(bridge.mutex);
      bridge.heartbeat.store(now_ms());
      bridge.waiting.store(false, std::memory_order_release);
    }
  }
}
bool admit_worker(KernelRuntime &runtime, std::string &error) {
  {
    std::lock_guard lock(bridge.mutex);
    const auto state = bridge.state.load();
    if (state == IoStartState::Starting || state == IoStartState::Ready) return true;
    // A function return alone is not managed-wrapper retirement. Preserve the
    // old reservation until task_active says it has actually been released.
    if (runtime.task_active("maintenance_io", ManagedTaskOwnerKind::Subsystem, kMaintenanceOwner)) return true;
    bridge.transition_started.store(now_ms());
    bridge.state.store(IoStartState::Starting);
    bridge.retry_pending = false;
  }
  ManagedTaskDefinition definition{.id = "maintenance_io", .owner_service_id = kMaintenanceOwner,
      .role = "blocking_io", .stack_bytes = 12288, .priority = kMaintenanceIoPriority,
      .core_affinity = kMaintenanceIoCore, .lifetime = ManagedTaskLifetime::Feature,
      .memory = ManagedTaskMemory::Internal};
  definition.owner_kind = ManagedTaskOwnerKind::Subsystem;
  if (!create_managed_task(runtime, definition, io_task, &runtime, nullptr, error)) {
    bridge.state.store(IoStartState::Failed);
    return false;
  }
  return true;
}
}
ServiceExecutorDefinition maintenance_io_executor(uint8_t queue_depth) {
  return {kNetworkIoServiceExecutor, kMaintenanceOwner, queue_depth, {request_executor, &bridge}};
}
void poll_maintenance_io(KernelRuntime &runtime) {
  poll_application_io(runtime);
  {
    std::lock_guard lock(bridge.mutex);
    if (bridge.runtime != &runtime || !runtime.current_task_owns_executor(kMainServiceExecutor)) return;
    const auto state = bridge.state.load();
    if (state == IoStartState::Ready || state == IoStartState::Starting) return;
    if (bridge.retry_pending && static_cast<int32_t>(now_ms() - bridge.retry_at) < 0) return;
    if (!bridge.requested && work_delay() > 1 && !runtime.executor_pending_calls(kNetworkIoServiceExecutor)) return;
  }
  // The Network manager can reset its callback when stopping. Reinstall the
  // same immutable bridge at admission, and due-deadline polling is a fallback.
  set_network_job_wake_handler(request_work, &bridge);
  std::string error;
  const bool admitted = admit_worker(runtime, error);
  std::lock_guard lock(bridge.mutex);
  if (!admitted) {
    bridge.retry_pending = true;
    bridge.retry_at = now_ms() + kRetryDelayMs;
  }
}
bool start_maintenance_io(KernelRuntime &runtime, apps::ApplicationsService &applications, std::string &error) {
  error.clear();
  {
    std::lock_guard lock(bridge.mutex);
    if (bridge.runtime && (bridge.runtime != &runtime || bridge.applications != &applications)) {
      error = "maintenance_io_runtime_owner_mismatch";
      return false;
    }
    if(!runtime.declare_task_owner(ManagedTaskOwnerKind::Subsystem,kMaintenanceOwner,error))return false;
    if (!bridge.runtime) {
      bridge.runtime = &runtime;
      bridge.applications = &applications;
    }
    bridge.requested = true;
  }
  set_network_job_wake_handler(request_work, &bridge);
  if (!configure_application_io(runtime, applications, error)) return false;
  if (!admit_worker(runtime, error)) return false;
  const auto started = now_ms();
  for (;;) {
    const auto state = bridge.state.load(std::memory_order_acquire);
    if (state == IoStartState::Ready) return true;
    if (state == IoStartState::Failed) { error = "maintenance_io_bind_failed"; return false; }
    if (now_ms() - started > kWorkerStartTimeoutMs) { error = "maintenance_io_timeout"; return false; }
    vTaskDelay(1);
  }
}
bool maintenance_io_owned_by(const KernelRuntime &runtime) {
  std::lock_guard lock(bridge.mutex);
  return bridge.runtime == &runtime;
}
bool product_runtime_io_healthy(const KernelRuntime &runtime) {
  if (!application_io_healthy(runtime)) return false;
  // Observe one coherent worker state. In particular, do not observe an old
  // heartbeat after leaving the lock that protects bounded-work transitions.
  std::lock_guard lock(bridge.mutex);
  if (bridge.runtime != &runtime) return false;
  const auto state = bridge.state.load(std::memory_order_acquire);
  const auto heartbeat = bridge.heartbeat.load();
  const auto now = now_ms();
  const uint32_t elapsed = now - heartbeat;
  const uint32_t wait = bridge.wait_ms.load();
  const bool waiting = bridge.waiting.load(std::memory_order_acquire);
  const auto observed = [&](bool healthy) {
#ifdef ESP_PLATFORM
    static bool previous = true;
    if (healthy != previous) {
#if defined(LUNAROS_DIAGNOSTIC_PROBES)
      const auto phase=diagnostic_phase.load();
      ESP_LOGI("diag.io_phase", "healthy=%d phase=%lu phase_age_ms=%lu mask=%lu",
          healthy, static_cast<unsigned long>(phase),static_cast<unsigned long>(phase?(system::probe::wall_clock()-diagnostic_phase_started.load())/1000:0),static_cast<unsigned long>(system::probe::selected()));
#endif
      ESP_LOGI("runtime.io_health", "healthy=%d state=%u age_ms=%u waiting=%d wait_ms=%u bounded=%d remaining_ms=%ld",
          healthy, unsigned(state), unsigned(elapsed), waiting, unsigned(wait),
          bridge.bounded_work, long(int32_t(bridge.work_deadline - now)));
      previous = healthy;
    }
#endif
    return healthy;
  };
  if (state == IoStartState::Idle || state == IoStartState::Retiring) {
    const bool idle=!bridge.requested&&work_delay()>1&&bridge.runtime&&
        !bridge.runtime->executor_pending_calls(kNetworkIoServiceExecutor);
    return observed(bridge.ever_ready.load()&&(idle||(state==IoStartState::Retiring&&
        now-bridge.transition_started.load()<kWorkerStartTimeoutMs)));
  }
  if(state==IoStartState::Starting)return observed(bridge.ever_ready.load()&&
      now-bridge.transition_started.load()<kWorkerStartTimeoutMs);
  if (state != IoStartState::Ready) return observed(false);
  if (bridge.bounded_work) return observed(int32_t(now - bridge.work_deadline) < 1000);
  return observed(elapsed < 1000 || (waiting && (elapsed <= wait || elapsed - wait < 1000)));
}
} // namespace lunaredu::lunaros
