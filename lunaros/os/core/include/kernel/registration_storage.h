#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <utility>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace lunaredu::lunaros {
namespace registration_detail {
inline void *allocate(size_t bytes, bool external) {
#ifdef ESP_PLATFORM
  const bool psram = external && heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return heap_caps_malloc(bytes, MALLOC_CAP_8BIT | (psram ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL));
#else
  (void)external;
  return std::malloc(bytes);
#endif
}
inline void release(void *storage) {
#ifdef ESP_PLATFORM
  heap_caps_free(storage);
#else
  std::free(storage);
#endif
}
}

// Storage for trusted boot declarations and owner scratch space. Growth is
// fallible, with no fixed entry budget; callers allocate before polling starts.
template <class T> class RegistrationArray {
 public:
  explicit RegistrationArray(bool external = false) : external_(external) {}
  ~RegistrationArray() { clear(); }
  RegistrationArray(const RegistrationArray &) = delete;
  RegistrationArray &operator=(const RegistrationArray &) = delete;
  RegistrationArray(RegistrationArray &&other) noexcept { swap(other); }
  RegistrationArray &operator=(RegistrationArray &&other) noexcept {
    if (this != &other) { clear(); swap(other); }
    return *this;
  }
  bool resize(size_t count) {
    if (count > capacity_) {
      if (count > std::numeric_limits<size_t>::max() / sizeof(T)) return false;
      auto *next = static_cast<T *>(registration_detail::allocate(count * sizeof(T), external_));
      if (!next) return false;
      for (size_t i = 0; i < size_; ++i) ::new (static_cast<void *>(next + i)) T(std::move(data_[i]));
      for (size_t i = 0; i < size_; ++i) std::destroy_at(data_ + i);
      registration_detail::release(data_); data_ = next; capacity_ = count;
    }
    while (size_ > count) std::destroy_at(data_ + --size_);
    while (size_ < count) { ::new (static_cast<void *>(data_ + size_)) T(); ++size_; }
    return true;
  }
  void clear() {
    while (size_) std::destroy_at(data_ + --size_);
    registration_detail::release(data_); data_ = nullptr; capacity_ = 0;
  }
  void fill(const T &value) { std::fill_n(data_, size_, value); }
  T &operator[](size_t index) { return data_[index]; }
  const T &operator[](size_t index) const { return data_[index]; }
  T *data() { return data_; }
  const T *data() const { return data_; }
  T *begin() { return data_; }
  T *end() { return size_ ? data_ + size_ : data_; }
  const T *begin() const { return data_; }
  const T *end() const { return size_ ? data_ + size_ : data_; }
  size_t size() const { return size_; }
  size_t capacity() const { return capacity_; }
 private:
  void swap(RegistrationArray &other) noexcept {
    std::swap(data_, other.data_); std::swap(size_, other.size_);
    std::swap(capacity_, other.capacity_); std::swap(external_, other.external_);
  }
  T *data_ = nullptr;
  size_t size_ = 0, capacity_ = 0;
  bool external_ = false;
};

// Individual records never move as registrations are appended. Snapshots,
// queued executor calls and service-use tokens retain valid record pointers.
template <class T> class RegistrationTable {
 public:
  explicit RegistrationTable(bool external = false) : index_(external), external_(external) {}
  ~RegistrationTable() { clear(); }
  RegistrationTable(const RegistrationTable &) = delete;
  RegistrationTable &operator=(const RegistrationTable &) = delete;
  T *append() {
    if (size_ == std::numeric_limits<size_t>::max()) return nullptr;
    if (!index_.resize(size_ + 1)) return nullptr;
    void *storage = registration_detail::allocate(sizeof(T), external_);
    if (!storage) {
      if (size_) (void)index_.resize(size_);
      else index_.clear();
      return nullptr;
    }
    T *record = ::new (storage) T();
    index_[size_++] = record;
    return record;
  }
  void pop_back() {
    if (!size_) return;
    T *record = index_[--size_]; std::destroy_at(record); registration_detail::release(record);
    (void)index_.resize(size_);
  }
  void clear() { while (size_) pop_back(); index_.clear(); }
  void erase(size_t index) {
    T *record = index_[index];
    std::destroy_at(record);
    registration_detail::release(record);
    for (size_t following = index + 1; following < size_; ++following)
      index_[following - 1] = index_[following];
    --size_;
    if (size_) (void)index_.resize(size_);
    else index_.clear();
  }
  T &operator[](size_t i) { return *index_[i]; }
  const T &operator[](size_t i) const { return *index_[i]; }
  size_t size() const { return size_; }
  size_t storage_bytes() const { return size_ * sizeof(T) + index_.capacity() * sizeof(T *); }
 private:
  RegistrationArray<T *> index_;
  size_t size_ = 0;
  bool external_;
};
}
