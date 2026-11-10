#pragma once
#include <array>
#include <atomic>
#include <cstddef>

namespace lunaredu::lunaros::graphics {
// One producer and one consumer. Full queues reject new work; actions are
// never coalesced like render snapshots. No producer mutates the scene.
template<class T, size_t Capacity = 16> class Queue {
 public:
  bool push(const T &value) {
    const auto write = write_.load(std::memory_order_relaxed);
    const auto next = (write + 1) % (Capacity + 1);
    if (next == read_.load(std::memory_order_acquire)) return false;
    values_[write] = value;
    write_.store(next, std::memory_order_release);
    return true;
  }
  bool pop(T &value) {
    const auto read = read_.load(std::memory_order_relaxed);
    if (read == write_.load(std::memory_order_acquire)) return false;
    value = values_[read];
    read_.store((read + 1) % (Capacity + 1), std::memory_order_release);
    return true;
  }
 private:
  std::array<T, Capacity + 1> values_{};
  std::atomic<size_t> read_{0}, write_{0};
};
} // namespace lunaredu::lunaros::graphics
