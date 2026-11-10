#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
namespace lunaredu::lunaros::apps {
class ApplicationOperationSource;
enum class ApplicationOperationPhase { None, Pending, Succeeded, Failed, Cancelled };
struct ApplicationOperationResult {
  ApplicationOperationPhase phase = ApplicationOperationPhase::None;
  std::string error;
  uint64_t catalog_revision = 0;
};
// Retainable receipt for one explicit operation. The I/O owner publishes once;
// Main observes without a storage read, mutex, callback or worker allocation.
class ApplicationOperation {
public:
  explicit operator bool() const { return bool(state_); }
  bool same(const ApplicationOperation &other) const { return state_ && state_==other.state_; }
  bool pending() const {
    return state_ && state_->phase.load(std::memory_order_acquire) == ApplicationOperationPhase::Pending;
  }
  ApplicationOperationResult result() const {
    if (!state_) return {};
    const auto phase = state_->phase.load(std::memory_order_acquire);
    if (phase == ApplicationOperationPhase::Pending) return {phase, {}, 0};
    return {phase, state_->error, state_->catalog_revision};
  }
private:
  friend class ApplicationOperationSource;
  struct State {
    std::atomic<ApplicationOperationPhase> phase{ApplicationOperationPhase::Pending};
    std::string error;
    uint64_t catalog_revision = 0;
    void complete(ApplicationOperationPhase value, std::string detail = {}, uint64_t revision = 0) {
      error = std::move(detail); catalog_revision = revision;
      phase.store(value, std::memory_order_release);
    }
  };
  std::shared_ptr<State> state_;
};
// One producer, any number of read-only receipts. Construction is cold and
// explicit; abandoning queued work always leaves a terminal result for readers.
class ApplicationOperationSource {
public:
  ApplicationOperationSource() { receipt_.state_=std::make_shared<ApplicationOperation::State>(); }
  ApplicationOperationSource(const ApplicationOperationSource &)=delete;
  ApplicationOperationSource &operator=(const ApplicationOperationSource &)=delete;
  ~ApplicationOperationSource() {
    if(receipt_.pending())complete(ApplicationOperationPhase::Cancelled,"application_operation_abandoned");
  }
  ApplicationOperation receipt() const { return receipt_; }
  bool pending() const { return receipt_.pending(); }
  // Serialized by the operation's one producer; completion is immutable.
  void complete(ApplicationOperationPhase phase,std::string error={},uint64_t revision=0) {
    if(!pending() || phase==ApplicationOperationPhase::Pending || phase==ApplicationOperationPhase::None)return;
    receipt_.state_->complete(phase,std::move(error),revision);
  }
private:
  ApplicationOperation receipt_;
};
}
