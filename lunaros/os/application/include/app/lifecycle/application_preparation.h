#pragma once
#include <string>

namespace lunaredu::app {
enum class ApplicationPreparationState { Unprepared, Preparing, Ready, Failed };
struct ApplicationPreparationStatus {
  ApplicationPreparationState state = ApplicationPreparationState::Unprepared;
  std::string error;
};
// Input/lifecycle-owner mechanism, not a task or registry. Retrying is explicit;
// the operation retains its completed stages and borrowed-resource ownership.
// Ready preparation is idempotent. Failure never resets an unrelated subsystem.
class ApplicationPreparation {
 public:
  ApplicationPreparation() = default;
  ApplicationPreparation(const ApplicationPreparation &) = delete;
  ApplicationPreparation &operator=(const ApplicationPreparation &) = delete;
  const ApplicationPreparationStatus &status() const { return status_; }
  bool ready() const { return status_.state == ApplicationPreparationState::Ready; }
  template<class Operation> bool ensure(Operation operation, std::string &error) {
    if (ready()) { error.clear(); return true; }
    if (status_.state == ApplicationPreparationState::Preparing) {
      error = "application_preparation_busy"; return false;
    }
    status_.state = ApplicationPreparationState::Preparing;
    status_.error.clear();
    error.clear();
    const bool ok = operation(error);
    status_.state = ok ? ApplicationPreparationState::Ready : ApplicationPreparationState::Failed;
    if (ok) error.clear();
    else if (error.empty()) error = "application_preparation_failed";
    status_.error = error;
    return ok;
  }
 private:
  ApplicationPreparationStatus status_;
};
} // namespace lunaredu::app
