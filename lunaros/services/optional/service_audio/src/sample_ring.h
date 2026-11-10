#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <algorithm>
#include <cstring>

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
  void release() { data_.reset(); capacity_ = 0; clear(); }
  size_t size() const { return size_; }
  size_t discard(size_t count) {
    count=std::min(count,size_);
    if(count){head_=(head_+count)%capacity_;size_-=count;}
    return count;
  }
  // Same overwrite-oldest semantics as repeated push, using at most two spans.
  size_t push(const int16_t *samples,size_t count) {
    if(!count)return 0;
    if(!capacity_)return count;
    const size_t dropped=count>capacity_-size_?count-(capacity_-size_):0;
    if(count>=capacity_){
      std::memcpy(data_.get(),samples+count-capacity_,capacity_*sizeof(int16_t));
      head_=0;size_=capacity_;return dropped;
    }
    discard(dropped);
    const auto tail=(head_+size_)%capacity_,first=std::min(count,capacity_-tail);
    std::memcpy(data_.get()+tail,samples,first*sizeof(int16_t));
    if(first<count)std::memcpy(data_.get(),samples+first,(count-first)*sizeof(int16_t));
    size_+=count;return dropped;
  }
  size_t pop(int16_t *samples,size_t count) {
    count=std::min(count,size_);
    if(!count)return 0;
    const auto first=std::min(count,capacity_-head_);
    std::memcpy(samples,data_.get()+head_,first*sizeof(int16_t));
    if(first<count)std::memcpy(samples+first,data_.get(),(count-first)*sizeof(int16_t));
    discard(count);return count;
  }
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
