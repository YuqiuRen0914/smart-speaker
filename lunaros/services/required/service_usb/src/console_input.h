#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include "lunaros_device_protocol.h"

namespace lunaredu::lunaros {
// The USB owner separates optional product console lines from LP frames. Binary
// payloads (even with invalid CRCs) can never become product text commands.
class UsbConsoleInput {
 public:
  template<class Binary, class Text>
  void append(const uint8_t *bytes, size_t count, Binary binary, Text text) {
    for (size_t i = 0; i < count; ++i) {
      const uint8_t byte = bytes[i];
      if (frame_size_) {
        if (frame_size_ < header_.size()) header_[frame_size_] = byte;
        ++frame_size_;
        if (frame_size_ == header_.size()) {
          frame_total_ = 14 + size_t(header_[8]) + (size_t(header_[9]) << 8);
          accept_frame_ = frame_total_ <= kDeviceProtocolMaxPayloadSize + 14 &&
              header_[2] == kDeviceProtocolVersion && header_[3] != 0;
          if (accept_frame_) binary(header_.data(), header_.size(), false);
        } else if (frame_size_ > header_.size() && accept_frame_) {
          // The protocol decoder is the sole demand-owned binary buffer.
          // Final tells it to retire even a bad CRC before text resumes.
          binary(&byte, 1, frame_size_ == frame_total_);
        }
        if (frame_total_ && frame_size_ == frame_total_) {
          frame_size_ = frame_total_ = 0;
          accept_frame_ = false;
        }
        continue;
      }
      if (pending_l_) {
        pending_l_ = false;
        if (byte == 'P') {
          header_[0] = 'L'; header_[1] = 'P'; frame_size_ = 2;
          line_size_ = 0; discard_line_ = false;
          continue;
        }
        add_text('L');
      }
      if (byte == 'L') { pending_l_ = true; continue; }
      if (byte == '\r' || byte == '\n') {
        if (!discard_line_ && line_size_) {
          line_[line_size_] = '\0'; text(line_.data());
        }
        line_size_ = 0; discard_line_ = false;
      } else add_text(byte);
    }
  }
  void reset() {
    frame_size_ = frame_total_ = line_size_ = 0;
    pending_l_ = discard_line_ = accept_frame_ = false;
  }
 private:
  void add_text(uint8_t byte) {
    if (byte < 32 || byte > 126 || line_size_ + 1 >= line_.size()) {
      discard_line_ = true; return;
    }
    if (!discard_line_) line_[line_size_++] = static_cast<char>(byte);
  }
  std::array<uint8_t, 10> header_{};
  std::array<char, 128> line_{};
  size_t frame_size_ = 0, frame_total_ = 0, line_size_ = 0;
  bool pending_l_ = false, discard_line_ = false, accept_frame_ = false;
};
} // namespace lunaredu::lunaros
