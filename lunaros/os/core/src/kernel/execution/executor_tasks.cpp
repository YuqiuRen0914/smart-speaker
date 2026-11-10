#include "kernel/task_signal.h"
#include "kernel/runtime.h"
#include "kernel/executor_tasks.h"
#include "kernel/service_executor.h"
#include <algorithm>
#include <cstring>
#include <esp_timer.h>
#ifdef ESP_PLATFORM
#include <esp_log.h>
#endif
#include "diagnostics/runtime_work_profile.h"
#include "diagnostics/cpu_monitor.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace lunaredu::lunaros {
namespace {
uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
} // namespace
void ExecutorTaskGroup::run(void *opaque) {
  auto &entry = *static_cast<Entry *>(opaque);
  auto &group = *entry.group;
  while (group.gate_.load() == Gate::Hold)
    vTaskDelay(1);
  bool bound = false;
  if (group.gate_.load() != Gate::Cancel) {
    bound = group.runtime_.bind_executor(entry.spec.executor, &entry.error);
    entry.state.store(bound ? State::Bound : State::Failed);
  }
  if (bound) {
    while (group.gate_.load() == Gate::Bind)
      vTaskDelay(1);
    if (group.gate_.load() == Gate::Run) {
      if (entry.spec.enter)
        entry.spec.enter(entry.spec.context);
      entry.heartbeat.store(now_ms());
      entry.state.store(State::Running);
      uint32_t next_tick_ms = now_ms();
      system::RuntimeWorkProfile<3> profile;
      uint32_t ticks=0,skipped=0,notifications=0,fast_wakes=0,immediate=0,max_late_ms=0;
      uint64_t requested_wait_ms=0;
      auto finish_profile=[&] {
        profile.mark(2);
        const auto measured=profile.finish();
        if(measured.calls) {
#ifdef ESP_PLATFORM
          ESP_LOGI("executor.work","owner=%s calls=%u ticks=%u window_us=%llu worker_task_us=%u pump_us=%llu tick_us=%llu wait_us=%llu max_pump_us=%u max_tick_us=%u requested_wait_ms=%llu notifications=%u fast_wakes=%u immediate=%u skipped=%u max_late_ms=%u",
              entry.spec.executor,unsigned(measured.calls),unsigned(ticks),static_cast<unsigned long long>(measured.window_us),unsigned(measured.worker_task_us),
              static_cast<unsigned long long>(measured.wall[0]),static_cast<unsigned long long>(measured.wall[1]),static_cast<unsigned long long>(measured.wall[2]),
              unsigned(measured.maximum_us[0]),unsigned(measured.maximum_us[1]),static_cast<unsigned long long>(requested_wait_ms),unsigned(notifications),unsigned(fast_wakes),unsigned(immediate),unsigned(skipped),unsigned(max_late_ms));
#endif
        }
        if(measured.calls||!profile.active()){ticks=skipped=notifications=fast_wakes=immediate=max_late_ms=0;requested_wait_ms=0;}
      };
      auto wait_profiled=[&](uint32_t delay) {
        if(!profile.active()){task_signal::wait_ms(delay);return;}
        requested_wait_ms+=delay;
        const auto started=esp_timer_get_time();
        const auto received=task_signal::wait_ms(delay);
        notifications+=received;
        if(received&&esp_timer_get_time()-started<100)++fast_wakes;
      };
      while (group.gate_.load() == Gate::Run) {
        profile.begin(system::main_work_profiling_active());
        if(profile.fresh_window()){ticks=skipped=notifications=fast_wakes=immediate=max_late_ms=0;requested_wait_ms=0;}
        if (!group.runtime_.pump_executor(entry.spec.executor, 0,
                                          &entry.error)) {
          entry.state.store(State::Failed);
          // Keep the owner alive: admitted synchronous calls must never point
          // at a deleted task. An explicit group stop is the only safe point
          // to release that owner and finish the failed task.
          while (group.gate_.load() == Gate::Run)
            vTaskDelay(pdMS_TO_TICKS(100));
          break;
        }
        profile.mark(0);
        if(entry.spec.checkpoint)entry.spec.checkpoint(entry.spec.checkpoint_context,entry.spec.executor);
        if (entry.spec.period_ms != 0) {
          const uint32_t current_ms = now_ms();
          if (static_cast<int32_t>(current_ms - next_tick_ms) >= 0) {
            if(profile.active()){++ticks;max_late_ms=std::max(max_late_ms,current_ms-next_tick_ms);}
            if (entry.spec.tick)
              (void)entry.spec.tick(entry.spec.context);
            entry.heartbeat.store(now_ms());
            const int32_t overdue_ms =
                static_cast<int32_t>(now_ms() - next_tick_ms);
            if(profile.active())skipped+=static_cast<uint32_t>(std::max<int32_t>(0,overdue_ms))/entry.spec.period_ms;
            next_tick_ms +=
                (static_cast<uint32_t>(std::max<int32_t>(0, overdue_ms)) /
                     entry.spec.period_ms +
                 1) *
                entry.spec.period_ms;
          }
          profile.mark(1);
          const int32_t remaining_ms =
              static_cast<int32_t>(next_tick_ms - now_ms());
          if (remaining_ms <= 0) {
            if(profile.active())++immediate;
            finish_profile();
            continue;
          }
          wait_profiled(std::min<uint32_t>(
                                      static_cast<uint32_t>(remaining_ms), 100));
          finish_profile();
          continue;
        }
        const uint32_t delay =
            entry.spec.tick ? entry.spec.tick(entry.spec.context) : 10;
        if(profile.active())++ticks;
        entry.heartbeat.store(now_ms());
        profile.mark(1);
        wait_profiled(std::max<uint32_t>(1,std::min<uint32_t>(delay,100)));
        finish_profile();
      }
    }
    // Stop product ticks, then atomically reject foreign requests while the
    // existing owner completes accepted calls. leave may tear down their
    // contexts, so it runs only after the admission queue is drained.
    while (!group.runtime_.prepare_executor_retirement(entry.spec.executor, &entry.error)) {
      (void)group.runtime_.pump_executor(entry.spec.executor, 0, &entry.error);
      task_signal::wait_ms(10);
    }
    if (entry.state.load() == State::Running || entry.state.load() == State::Failed) {
      if (entry.spec.leave) entry.spec.leave(entry.spec.context);
    }
    // A refusal is never permission to delete an owner. Preserve task/specs
    // through a stop timeout until withdrawal really succeeds.
    while (!group.runtime_.unbind_executor(entry.spec.executor, &entry.error))
      task_signal::wait_ms(10);
  }
  if (entry.state.load() != State::Failed)
    entry.state.store(State::Exited);
}
bool ExecutorTaskGroup::released() const {
  for (size_t i = 0; i < count_; ++i)
    if (runtime_.task_active(entries_[i].spec.task.id))return false;
  return true;
}
bool ExecutorTaskGroup::cancel(std::string &error) {
  gate_.store(Gate::Cancel);
  const uint32_t started = now_ms();
  while (!released()) {
    if (now_ms() - started >= 5000) {
      if (!error.empty()) error += ";";
      error += "executor_task_cleanup_pending";
      return false;
    }
    vTaskDelay(1);
  }
  return true;
}
bool ExecutorTaskGroup::start(std::span<const ExecutorTaskSpec> tasks,
                              std::string &error) {
  error.clear();
  std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) { error = "executor_task_operation_in_progress"; return false; }
  if (gate_.load() == Gate::Run) {
    error = "executor_tasks_already_started";
    return false;
  }
  if (!released()) {
    error = "executor_task_cleanup_pending";
    return false;
  }
  if (tasks.size() == 0 || tasks.size() > entries_.size()) {
    error = "invalid_executor_task_count";
    return false;
  }
  for (auto it = tasks.begin(); it != tasks.end(); ++it) {
    if (!it->executor || !*it->executor || !it->task.id || !*it->task.id ||
        it->period_ms > 1000) {
      error = "invalid_executor_task";
      return false;
    }
    for (auto prior = tasks.begin(); prior != it; ++prior)
      if (std::strcmp(it->executor, prior->executor) == 0 ||
          std::strcmp(it->task.id, prior->task.id) == 0) {
        error = "duplicate_executor_task";
        return false;
      }
  }
  gate_.store(Gate::Hold);
  count_ = 0;
  for (const auto &spec : tasks) {
    auto &entry = entries_[count_++];
    entry.group = this;
    entry.spec = spec;
    entry.error.clear();
    entry.state.store(State::Starting);
    if (!create_managed_task(runtime_, spec.task, run, &entry, nullptr,
                             error)) {
      entry.state.store(State::Idle);
      --count_;
      cancel(error);
      return false;
    }
  }
  gate_.store(Gate::Bind);
  const uint32_t started = now_ms();
  for (;;) {
    bool all_bound = true;
    for (size_t i = 0; i < count_; ++i) {
      const auto state = entries_[i].state.load();
      if (state == State::Failed) {
        error = entries_[i].error;
        cancel(error);
        return false;
      }
      all_bound = all_bound && state == State::Bound;
    }
    if (all_bound) {
      gate_.store(Gate::Run);
      return true;
    }
    if (now_ms() - started >= 5000) {
      error = "service_executor_bind_timeout";
      cancel(error);
      return false;
    }
    vTaskDelay(1);
  }
}

bool ExecutorTaskGroup::stop(std::string &error) {
  error.clear();
  std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) { error = "executor_task_operation_in_progress"; return false; }
  for (size_t i = 0; i < count_; ++i) {
    if (runtime_.current_task_owns_executor(entries_[i].spec.executor)) {
      error = "executor_task_stop_from_owner"; return false;
    }
  }
  if (released()) {
    count_ = 0;
    gate_.store(Gate::Cancel);
    return true;
  }
  if (!cancel(error)) {
    return false;
  }
  count_ = 0;
  return true;
}

bool ExecutorTaskGroup::healthy(uint32_t maximum_silence_ms) const {
  // A hot-path observation must neither race spec replacement nor wait for
  // the coordinator's bounded startup/stop join.
  std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock() || gate_.load() != Gate::Run)
    return false;
  for (size_t i = 0; i < count_; ++i) {
    const auto state = entries_[i].state.load();
    const auto heartbeat = entries_[i].heartbeat.load();
    // A worker may publish a newer heartbeat while we inspect another entry.
    // Read the clock after this entry's heartbeat to avoid unsigned underflow.
    const auto now = now_ms();
    if (state != State::Running || now - heartbeat >= maximum_silence_ms) {
#ifdef ESP_PLATFORM
      if (system::cpu_monitor_active()) {
        static std::atomic<uint32_t> last_report_ms{0};
        auto previous = last_report_ms.load();
        if (now - previous >= 2000 && last_report_ms.compare_exchange_strong(previous, now)) {
          char executor[64]{};
          std::strncpy(executor, entries_[i].spec.executor, sizeof(executor) - 1);
          lock.unlock();
          ESP_LOGI("executor.health", "owner=%s state=%u now_ms=%u heartbeat_ms=%u signed_age_ms=%ld limit_ms=%u",
              executor, unsigned(state), unsigned(now), unsigned(heartbeat),
              long(static_cast<int32_t>(now - heartbeat)), unsigned(maximum_silence_ms));
        }
      }
#endif
      return false;
    }
  }
  return true;
}
} // namespace lunaredu::lunaros
