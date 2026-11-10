#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <memory>

namespace lunaredu::lunaros {

inline constexpr const char *kMainServiceExecutor = "lunar.main";
inline constexpr const char *kNetworkServiceExecutor = "lunar.network";
inline constexpr const char *kNetworkIoServiceExecutor = "lunar.netio";

struct ServiceOperationResult {
  bool ok = true;
  std::string error;
  // Optional inner work outcome; does not alter executor/transport success.
  bool work_result_known = false;
  bool work_ok = true;
  const char *work_error_code = nullptr; // Borrowed until callback returns to dispatcher.
};

using ServiceOperation = void (*)(void *context,
                                  ServiceOperationResult &result);

// Own every argument and any callback state until execute returns. Borrowed
// service ports must remain alive until executor retirement, as declarations do.
// work_error_code, if used, must point into this request (never a local buffer).
// Destruction may occur on either task: own ordinary data here; SDK resources
// and asynchronous activities remain with their checked domain owner.
// Owning a request grants no authority: recheck domain authorization in execute.
class ServiceRequest {
public:
  virtual ~ServiceRequest() = default;
  virtual void execute(ServiceOperationResult &) = 0;
};

// Move-only observation of accepted work. Dropping the ticket does not cancel
// side effects or free the queued request. Only cancel_before_start can prevent
// execution; running work must use its domain's cooperative cancellation.
// A ticket has one observing owner. Ready means callback returned, not that a
// domain's separately accepted hardware activity has reached its final state.
enum class ServiceCallPhase:uint8_t { Invalid, Queued, Running, CancellationRequested, Completed };
class ServiceCall {
public:
  struct State;
  ServiceCall() = default;
  ~ServiceCall();
  ServiceCall(ServiceCall &&) noexcept;
  ServiceCall &operator=(ServiceCall &&) noexcept;
  ServiceCall(const ServiceCall &) = delete;
  ServiceCall &operator=(const ServiceCall &) = delete;
  bool valid() const { return state_ != nullptr; }
  bool ready() const;
  ServiceCallPhase phase() const; // Copied atomic observation, no wait or I/O.
  bool cancel_before_start(); // Idempotent acceptance; ready waits for owner drain.
  const ServiceOperationResult *result() const; // Stable while ticket is held.
  const ServiceRequest *request() const; // Result payload, only when ready.
  void reset();
private:
  State *state_ = nullptr;
  friend class ServiceExecutorRegistry;
};

// Optional demand signal for a dormant owner. Borrowed and immutable until
// registry reset; its context must outlive all invocations. Called outside the
// admission gate on the invoking task, so it must be nonblocking and must not
// synchronously invoke this executor. It signals the composition owner to
// admit/bind a worker; it never runs the requested operation on the caller.
struct ServiceExecutorActivation {
  void (*request)(void *context) = nullptr;
  void *context = nullptr;
};

enum class ExecutorReadiness:uint8_t { Ready, Pending, Unavailable };

struct ServiceExecutorDefinition {
  const char *id = nullptr;
  const char *owner_service_id = nullptr;
  uint8_t queue_depth = 8;
  ServiceExecutorActivation activation{};
};

struct ServiceExecutorSnapshot {
  const char *id = nullptr;
  const char *owner_service_id = nullptr;
  uint8_t queue_depth = 0;
  bool bound = false;
  uint32_t pending_calls = 0;
};

} // namespace lunaredu::lunaros
