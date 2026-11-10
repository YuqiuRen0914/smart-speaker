#pragma once
#include "lunaredu_lunaros/kernel/managed_task.h"
#include <array>
#include <atomic>
#include <initializer_list>
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
};

// Product-lifetime object. Startup admits the whole group, binds all owners,
// then releases work. Failed startup cooperatively releases tasks and owners.
// Services must only be started after start() succeeds.
class ExecutorTaskGroup {
public:
  bool start(std::initializer_list<ExecutorTaskSpec> tasks, std::string &error);
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
  std::mutex mutex_;
  std::array<Entry, 4> entries_{};
  size_t count_ = 0;
  std::atomic<Gate> gate_{Gate::Hold};
};
} // namespace lunaredu::lunaros
