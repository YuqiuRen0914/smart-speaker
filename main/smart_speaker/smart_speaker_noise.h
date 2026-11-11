#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace smart_speaker::noise {
// Seekable PCM WAV source for the existing audio_player. No SD file, worker,
// I2S ownership or mutable business state is created here.
class Stream {
 public:
  static constexpr uint32_t kSampleRate = 22050;
  static constexpr uint32_t kDataBytes = 0x7fff0000;
  static constexpr uint32_t kFileBytes = kDataBytes + 44;
  size_t read(char *data, size_t size);
  int64_t seek(int64_t offset, int origin);
 private:
  uint32_t position_ = 0;
};
FILE *open(); // Caller owns on failure to enqueue; decoder closes on success.
}  // namespace smart_speaker::noise
