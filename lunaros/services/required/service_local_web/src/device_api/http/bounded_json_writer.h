#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace lunaredu::lunaros::internal {
// Own only a fixed staging window, never a second copy of the whole document.
// Sender must report false for any incomplete write. Failure is terminal: no
// further bytes or successful stream terminator may follow a truncated body.
template<class Sender, size_t Capacity = 1024> class BoundedJsonWriter {
 public:
  explicit BoundedJsonWriter(Sender sender) : sender_(std::move(sender)) {}
  size_t write(uint8_t byte) { return write(&byte, 1); }
  size_t write(const uint8_t *data, size_t size) {
    if (failed_ || finished_ || (size && !data)) return 0;
    size_t consumed=0;
    while (consumed<size) {
      if (used_==Capacity && !flush(false)) return consumed;
      const size_t take=(size-consumed < Capacity-used_) ? size-consumed : Capacity-used_;
      std::memcpy(buffer_.data()+used_, data+consumed, take);
      used_+=take;consumed+=take;
    }
    return consumed;
  }
  bool finish() {
    if (finished_) return !failed_;
    finished_=true;
    return !failed_ && flush(true);
  }
 private:
  bool flush(bool final) {
    if (!sender_(buffer_.data(),used_,final)) {failed_=true;return false;}
    used_=0;return true;
  }
  static_assert(Capacity>0);
  Sender sender_;
  std::array<char,Capacity> buffer_{};
  size_t used_=0;
  bool failed_=false,finished_=false;
};
}
