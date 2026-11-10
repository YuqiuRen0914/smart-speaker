#include "lunaredu_lunaros/kernel/executor_tasks.h"
#include "lunaredu_lunaros/kernel/service_executor.h"
#include <algorithm>
#include <cstring>
#include <esp_timer.h>
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
    bound = bind_service_executor_to_current_task(entry.spec.executor,
                                                  &entry.error);
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
      while (group.gate_.load() == Gate::Run) {
        if (!pump_service_executor(entry.spec.executor, 0, &entry.error)) {
          entry.state.store(State::Failed);
          // Keep the owner alive: admitted synchronous calls must never point
          // at a deleted task. Health/OTA reports the failure to the product.
          for (;;)
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        const uint32_t delay =
            entry.spec.tick ? entry.spec.tick(entry.spec.context) : 10;
        entry.heartbeat.store(now_ms());
        ulTaskNotifyTake(pdTRUE,
                         std::max<TickType_t>(
                             1, pdMS_TO_TICKS(std::min<uint32_t>(delay, 100))));
      }
      if (entry.spec.leave)
        entry.spec.leave(entry.spec.context);
    }
    (void)unbind_service_executor_from_current_task(entry.spec.executor,
                                                    nullptr);
  }
  if (entry.state.load() != State::Failed)
    entry.state.store(State::Exited);
}
bool ExecutorTaskGroup::released() const {
  const auto snapshot = managed_task_registry_snapshot();
  for (const auto &task : snapshot.tasks) {
    if (!task.active)
      continue;
    for (size_t i = 0; i < count_; ++i)
      if (std::strcmp(task.id.data(), entries_[i].spec.task.id) == 0)
        return false;
  }
  return true;
}
bool ExecutorTaskGroup::cancel(std::string &error) {
  gate_.store(Gate::Cancel);
  const uint32_t started = now_ms();
  while (!released()) {
    if (now_ms() - started >= 5000) {
      error += ":executor_task_cleanup_pending";
      return false;
    }
    vTaskDelay(1);
  }
  return true;
}
bool ExecutorTaskGroup::start(std::initializer_list<ExecutorTaskSpec> tasks,
                              std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  error.clear();
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
    if (!it->executor || !*it->executor || !it->task.id || !*it->task.id) {
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
    if (!create_managed_task(spec.task, run, &entry, nullptr, error)) {
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
bool ExecutorTaskGroup::healthy(uint32_t maximum_silence_ms) const {
  if (gate_.load() != Gate::Run)
    return false;
  const auto now = now_ms();
  for (size_t i = 0; i < count_; ++i)
    if (entries_[i].state.load() != State::Running ||
        now - entries_[i].heartbeat.load() >= maximum_silence_ms)
      return false;
  return true;
}
} // namespace lunaredu::lunaros
