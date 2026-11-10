#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <initializer_list>

namespace lunaredu::lunaros {

inline constexpr const char *kMainServiceExecutor = "lunar.main";
inline constexpr const char *kNetworkServiceExecutor = "lunar.network";
inline constexpr const char *kNetworkIoServiceExecutor = "lunar.netio";
inline constexpr const char *kPythonServiceExecutor = "lunar.python";

struct ServiceOperationResult {
  bool ok = true;
  std::string error;
};

using ServiceOperation = void (*)(void *context,
                                  ServiceOperationResult &result);

struct ServiceExecutorDefinition {
  const char *id = nullptr;
  const char *owner_service_id = nullptr;
  uint8_t queue_depth = 8;
};

struct ServiceExecutorSnapshot {
  const char *id = nullptr;
  const char *owner_service_id = nullptr;
  uint8_t queue_depth = 0;
  bool bound = false;
};

// Installs only the product-declared bounded queues, never tasks. Repeating
// an identical declaration is allowed; replacement of a live registry is not.
bool setup_service_executors(std::initializer_list<ServiceExecutorDefinition> definitions,
                             std::string *error = nullptr);

bool register_service_executor(const ServiceExecutorDefinition &definition,
                               std::string *error = nullptr);
bool bind_service_executor_to_current_task(const char *executor_id,
                                           std::string *error = nullptr);
// Startup cancellation only: owner must have no admitted operations.
bool unbind_service_executor_from_current_task(const char *executor_id,
                                               std::string *error = nullptr);
// Only the bound owner task may drain an executor. Returning false is a hard
// ownership violation; callers must never turn it into a second consumer.
bool pump_service_executor(const char *executor_id, uint32_t wait_timeout_ms,
                           std::string *error = nullptr);
size_t service_executor_snapshot(ServiceExecutorSnapshot *output,
                                 size_t capacity);
bool current_task_owns_service_executor();

// Posts one in-process operation to a fixed executor and waits for completion.
// admission_timeout_ms only bounds queue admission. Once admitted, the caller
// remains alive until the operation completes, so typed stack contexts cannot
// become dangling pointers. Long-running or cancellable work belongs in a
// service-owned job manager rather than this executor.
bool invoke_service_operation(const char *executor_id,
                              ServiceOperation operation, void *context,
                              std::string &error,
                              uint32_t admission_timeout_ms = 100);

template <typename Context>
using TypedServiceOperation = void (*)(Context &context,
                                       ServiceOperationResult &result);

template <typename Context>
struct TypedServiceInvocation {
  TypedServiceOperation<Context> operation = nullptr;
  Context *context = nullptr;
};

template <typename Context>
void execute_typed_service_invocation(void *opaque,
                                      ServiceOperationResult &result) {
  auto *invocation = static_cast<TypedServiceInvocation<Context> *>(opaque);
  if (invocation == nullptr || invocation->operation == nullptr ||
      invocation->context == nullptr) {
    result.ok = false;
    result.error = "invalid_service_operation";
    return;
  }
  invocation->operation(*invocation->context, result);
}

template <typename Context>
bool invoke_service_operation(const char *executor_id,
                              TypedServiceOperation<Context> operation,
                              Context &context, std::string &error,
                              uint32_t admission_timeout_ms = 100) {
  TypedServiceInvocation<Context> invocation{operation, &context};
  return invoke_service_operation(
      executor_id, execute_typed_service_invocation<Context>, &invocation,
      error, admission_timeout_ms);
}

} // namespace lunaredu::lunaros
