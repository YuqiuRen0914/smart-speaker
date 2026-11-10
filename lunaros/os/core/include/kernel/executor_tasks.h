#pragma once
#include "kernel/managed_task.h"
#include "kernel/runtime.h"
#include <array>
#include <atomic>
#include <initializer_list>
#include <span>
#include <mutex>

namespace lunaredu::lunaros {
struct ExecutorTaskSpec {
  ManagedTaskDefinition task;
  const char *executor = nullptr;
  // One bounded iteration, returning the desired notification wait in ms.
  uint32_t (*tick)(void *) = nullptr;
  void *context = nullptr;
  void (*enter)(void *) = nullptr;
  void (*leave)(void *) = nullptr;
  // Optional fixed start-to-start period. The callback return is ignored;
  // missed periods are skipped, and executor notifications do not run it early.
  uint32_t period_ms = 0;
  void (*checkpoint)(void *,const char *) = nullptr;
  void *checkpoint_context = nullptr;
};

// Product-lifetime object. Startup admits the whole group, binds all owners,
// then releases work. Failed startup cooperatively releases tasks and owners.
// Services must only be started after start() succeeds.
class ExecutorTaskGroup {
public:
  explicit ExecutorTaskGroup(KernelRuntime &runtime) : runtime_(runtime) {}
  bool start(std::span<const ExecutorTaskSpec> tasks,std::string &error);
  bool start(std::initializer_list<ExecutorTaskSpec> tasks,std::string &error){return start(std::span<const ExecutorTaskSpec>{tasks.begin(),tasks.size()},error);}
  // Cooperatively releases every task and executor owner in the group. This
  // is safe after a failed or already-stopped startup. Call outside this group
  // after stopping dependent services. Admission closes before accepted work
  // drains and leave runs; a timeout retains owners and can be retried.
  bool stop(std::string &error);
  bool healthy(uint32_t maximum_silence_ms = 2000) const;

private:
  enum class Gate { Hold, Bind, Run, Cancel };
  enum class State { Idle, Starting, Bound, Running, Failed, Exited };
  struct Entry {
    ExecutorTaskGroup *group = nullptr;
    ExecutorTaskSpec spec;
    std::atomic<State> state{State::Idle};
    std::atomic<uint32_t> heartbeat{0};
    std::string error;
  };
  static void run(void *opaque);
  bool cancel(std::string &error);
  bool released() const;
  mutable std::mutex mutex_;
  std::array<Entry, 4> entries_{};
  size_t count_ = 0;
  std::atomic<Gate> gate_{Gate::Hold};
  KernelRuntime &runtime_;
};
} // namespace lunaredu::lunaros
