#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace lunaredu::pet::conversation::internal {

// All access uses the assistant runtime mutex. Bound both message count and
// payload bytes; a count-only queue would still admit arbitrarily large JSON.
class PendingMcpMessages {
public:
  static constexpr size_t kCapacity = 16;
  static constexpr size_t kMaximumBytes = 64U * 1024U;
  static constexpr size_t kMaximumMessageBytes = 32U * 1024U;

  bool can_accept(size_t count, size_t bytes) const {
    return count <= kCapacity - count_ - reserved_results_ &&
        bytes <= kMaximumBytes - bytes_ - reserved_results_ * kMaximumMessageBytes;
  }
  bool reserve_result() {
    if (!can_accept(1, kMaximumMessageBytes)) return false;
    ++reserved_results_; return true;
  }
  void release_result() { if (reserved_results_) --reserved_results_; }
  uint64_t generation() const { return generation_; }
  bool try_push(std::string message) {
    if (message.size() > kMaximumMessageBytes ||
        !can_accept(1, message.size())) return false;
    bytes_ += message.size();
    messages_[(head_ + count_) % kCapacity] = std::move(message);
    ++count_;
    return true;
  }
  // Playback receipts must leave before a terminal event rebuilds the session.
  bool try_push_front(std::string message) {
    if (message.size() > kMaximumMessageBytes || !can_accept(1, message.size())) return false;
    head_ = (head_ + kCapacity - 1) % kCapacity;
    bytes_ += message.size(); messages_[head_] = std::move(message); ++count_;
    return true;
  }
  bool empty() const { return count_ == 0; }
  size_t size() const { return count_; }
  size_t bytes() const { return bytes_; }
  std::string pop() {
    if (empty()) return {};
    bytes_ -= messages_[head_].size();
    std::string result = std::move(messages_[head_]);
    head_ = (head_ + 1) % kCapacity;
    --count_;
    return result;
  }
  void clear() {
    while (!empty()) (void)pop();
    head_ = 0;
    ++generation_;
    // An executing callback still owns its reservation until it relocks. Its
    // old result is discarded by generation, not delivered to a new session.
  }
private:
  std::array<std::string, kCapacity> messages_{};
  size_t head_ = 0;
  size_t count_ = 0;
  size_t bytes_ = 0;
  size_t reserved_results_ = 0;
  uint64_t generation_ = 0;
};

} // namespace lunaredu::pet::conversation::internal
