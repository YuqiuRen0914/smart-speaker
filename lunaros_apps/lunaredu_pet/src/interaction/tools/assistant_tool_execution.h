#pragma once
#include "interaction/tools/assistant_mcp_queue.h"
#include <mutex>

namespace lunaredu::pet::conversation::internal {
enum class ToolExecutionDisposition { Completed, QueueBusy, SessionChanged };

// Called with the runtime lock owned, returns with it owned. Response capacity
// remains reserved while other producers/reset paths are allowed to progress.
template <typename Function>
ToolExecutionDisposition execute_unlocked_tool(PendingMcpMessages &messages,
                                               std::unique_lock<std::mutex> &lock,
                                               Function &&function) {
  if (!messages.reserve_result()) return ToolExecutionDisposition::QueueBusy;
  struct Reservation {
    PendingMcpMessages &messages;
    ~Reservation() { messages.release_result(); }
  } reservation{messages};
  const uint64_t generation = messages.generation();
  {
    struct Unlock {
      std::unique_lock<std::mutex> &lock;
      explicit Unlock(std::unique_lock<std::mutex> &value) : lock(value) { lock.unlock(); }
      ~Unlock() { lock.lock(); }
    } unlock{lock};
    function();
  }
  return generation == messages.generation() ? ToolExecutionDisposition::Completed
                                              : ToolExecutionDisposition::SessionChanged;
}
}
