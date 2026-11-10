#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

namespace lunaredu::audio {
class SampleRing {
  std::unique_ptr<int16_t[]> data_;
  size_t capacity_ = 0, head_ = 0, size_ = 0;
public:
  bool initialize(size_t capacity) {
    if (capacity == 0) return false;
    std::unique_ptr<int16_t[]> data(new (std::nothrow) int16_t[capacity]);
    if (!data) return false;
    data_ = std::move(data); capacity_ = capacity; clear(); return true;
  }
  void clear() { head_ = size_ = 0; }
  size_t size() const { return size_; }
  bool push(int16_t value) {
    if (capacity_ == 0) return true;
    const bool dropped = size_ == capacity_;
    if (dropped) { head_ = (head_ + 1) % capacity_; --size_; }
    data_[(head_ + size_) % capacity_] = value; ++size_;
    return dropped;
  }
  int16_t pop() {
    if (size_ == 0) return 0;
    const int16_t value = data_[head_];
    head_ = (head_ + 1) % capacity_; --size_; return value;
  }
};
} // namespace lunaredu::audio
