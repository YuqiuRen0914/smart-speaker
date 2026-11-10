#include "kernel/services/service_host_internal.h"
#include "kernel/runtime.h"

#include <cstring>

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>
#include "kernel/service_executor.h"
#endif


namespace lunaredu::lunaros {

namespace {
struct ExecutorLifecycleInvocation {
  ServiceLifecycleAction action = nullptr;
  void *action_context = nullptr;
};

void run_executor_lifecycle(void *context, ServiceOperationResult &result) {
  auto *invocation = static_cast<ExecutorLifecycleInvocation *>(context);
  if (invocation == nullptr || invocation->action == nullptr) {
    result.ok = false;
    result.error = "invalid_service_lifecycle_action";
    return;
  }
  result.ok = invocation->action(invocation->action_context, result.error);
}
bool direct_lifecycle_dispatch(const char *, ServiceLifecycleAction action,
                               void *context, std::string &error) {
  if (action == nullptr) {
    error = "invalid_service_lifecycle_action";
    return false;
  }
  return action(context, error);
}
}

bool ServiceHost::dispatch(const ServiceNode &node,
                           ServiceLifecycleAction action, void *context,
                           std::string &error) const {
  const char *owner = node.definition.owner_executor != nullptr
                          ? node.definition.owner_executor
                          : "lunar.main";
  if (dispatcher_ != nullptr) {
    return dispatcher_(owner, action, context, error);
  }
  if (executors_ != nullptr && executors_->snapshot(nullptr, 0) != 0) {
    ExecutorLifecycleInvocation invocation{action, context};
    return executors_->invoke(owner, run_executor_lifecycle, &invocation,
                              error);
  }
  return direct_lifecycle_dispatch(owner, action, context, error);
}

} // namespace lunaredu::lunaros
