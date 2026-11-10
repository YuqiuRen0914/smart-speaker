#pragma once
#include "lunaros_device_protocol.h"
#include <utility>

namespace lunaredu::lunaros {
enum class UsbResponseProgress { Idle, Pending, Complete, Failed };

// Single response, owned by the existing USB poll owner. No FIFO or worker.
// The selected SDK accepts a complete <=224-byte write atomically or returns
// zero. Unexpected short writes retire the response instead of resuming a
// half-frame that could already have console logs inserted into it.
class UsbResponseOutput {
 public:
  bool begin(DeviceFrame &&response, int64_t now_us) {
    if (!encoder_.begin(std::move(response), 224)) return false;
    started_us_ = progress_us_ = now_us;
    return true;
  }
  bool pending() const { return encoder_.pending(); }
  template<class Write, class Clock>
  UsbResponseProgress poll(Write write, Clock clock) {
    if (!pending()) return UsbResponseProgress::Idle;
    const int64_t begin_us = clock();
    if (begin_us - progress_us_ >= 2'000'000 ||
        begin_us - started_us_ >= 10'000'000) {
      reset(); return UsbResponseProgress::Failed;
    }
    // Bound both attempts and elapsed encoding work. Zero-wait physical IO
    // must not turn an unplugged host or busy console into a product stall.
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
      if (wire_.empty() && !encoder_.encode_current(wire_)) {
        reset(); return UsbResponseProgress::Failed;
      }
      const int accepted = write(wire_.data(), wire_.size());
      if (accepted == 0) return UsbResponseProgress::Pending;
      if (accepted < 0 || static_cast<size_t>(accepted) != wire_.size()) {
        reset(); return UsbResponseProgress::Failed;
      }
      progress_us_ = clock();
      wire_.clear();
      encoder_.advance();
      if (!pending()) { reset(); return UsbResponseProgress::Complete; }
      if (progress_us_ - begin_us >= 500) break;
    }
    return UsbResponseProgress::Pending;
  }
  void reset() {
    encoder_.reset(); std::vector<uint8_t>{}.swap(wire_);
    started_us_ = progress_us_ = 0;
  }
 private:
  DeviceResponseEncoder encoder_;
  std::vector<uint8_t> wire_;
  int64_t started_us_ = 0, progress_us_ = 0;
};
} // namespace lunaredu::lunaros
