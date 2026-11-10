#pragma once
#include "device_api/device_operation.h"

namespace lunaredu::lunaros {
// Borrow immutable declarations without flattening/copying their definitions.
// The primary range, each additional range and the range descriptors themselves
// must outlive the sealed registry. Empty ranges support optional contributors.
class DeviceOperationSequence {
public:
  constexpr DeviceOperationSequence() = default;
  constexpr DeviceOperationSequence(const DeviceOperationDefinition *data, size_t count)
      : first_{data, count} {}
  constexpr DeviceOperationSequence(DeviceOperationsView first,
                                    const DeviceOperationsView *additional,
                                    size_t additional_count)
      : first_(first), additional_(additional), additional_count_(additional_count) {}

  bool valid(size_t maximum_operations) const {
    if (first_.count > maximum_operations || (first_.count && !first_.data) ||
        additional_count_ > maximum_operations ||
        (additional_count_ && !additional_)) return false;
    size_t total = first_.count;
    for (size_t i = 0; i < additional_count_; ++i) {
      const auto &range = additional_[i];
      if ((range.count && !range.data) || range.count > maximum_operations - total)
        return false;
      total += range.count;
    }
    return total != 0;
  }
  size_t size() const {
    size_t result = first_.count;
    for (size_t i = 0; i < additional_count_; ++i) result += additional_[i].count;
    return result;
  }
  const DeviceOperationDefinition *get(size_t index) const {
    if (index < first_.count) return first_.data + index;
    index -= first_.count;
    for (size_t i = 0; i < additional_count_; ++i) {
      const auto &range = additional_[i];
      if (index < range.count) return range.data + index;
      index -= range.count;
    }
    return nullptr;
  }
  // As with an array, indexing requires a validated sequence and index < size().
  const DeviceOperationDefinition &operator[](size_t index) const { return *get(index); }

private:
  DeviceOperationsView first_{};
  const DeviceOperationsView *additional_ = nullptr;
  size_t additional_count_ = 0;
};

} // namespace lunaredu::lunaros
