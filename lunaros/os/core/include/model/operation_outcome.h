#pragma once
#include <cstdint>
namespace lunaredu::lunaros {
// Completion describes the handler / domain acknowledgement, never an
// implicit hardware-stop guarantee. Physical retirement has its own owner.
enum class OperationCompletion : uint8_t { Completed, Accepted, NotStarted, Unknown };
enum class OperationError : uint8_t {
  None, InvalidArgument, NotFound, Forbidden, Conflict, Unavailable,
  Cancelled, DeadlineExceeded, Internal
};
struct OperationOutcome {
  OperationCompletion completion=OperationCompletion::NotStarted;
  OperationError category=OperationError::None;
};
constexpr const char *operation_completion_name(OperationCompletion value) {
  switch(value) {
  case OperationCompletion::Completed:return "completed";
  case OperationCompletion::Accepted:return "accepted";
  case OperationCompletion::NotStarted:return "not_started";
  case OperationCompletion::Unknown:return "unknown";
  }
  return "unknown";
}
constexpr const char *operation_error_name(OperationError value) {
  switch(value) {
  case OperationError::None:return "none";
  case OperationError::InvalidArgument:return "invalid_argument";
  case OperationError::NotFound:return "not_found";
  case OperationError::Forbidden:return "forbidden";
  case OperationError::Conflict:return "conflict";
  case OperationError::Unavailable:return "unavailable";
  case OperationError::Cancelled:return "cancelled";
  case OperationError::DeadlineExceeded:return "deadline_exceeded";
  case OperationError::Internal:return "internal";
  }
  return "internal";
}
}
