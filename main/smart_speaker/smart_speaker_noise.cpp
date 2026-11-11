#include "smart_speaker_noise.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <new>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#if !CONFIG_AUDIO_PLAYER_ENABLE_WAV
#error "White noise requires the existing audio_player WAV decoder; enable CONFIG_AUDIO_PLAYER_ENABLE_WAV"
#endif
#endif

namespace smart_speaker::noise {
namespace {
std::array<uint8_t, 44> header() {
  std::array<uint8_t, 44> out{};
  auto text = [&](size_t at, const char *value) { for (int i = 0; i < 4; ++i) out[at + i] = value[i]; };
  auto word = [&](size_t at, uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) out[at + i] = static_cast<uint8_t>(value >> (8 * i));
  };
  text(0, "RIFF"); word(4, Stream::kFileBytes - 8, 4); text(8, "WAVE");
  text(12, "fmt "); word(16, 16, 4); word(20, 1, 2); word(22, 2, 2);
  word(24, Stream::kSampleRate, 4); word(28, Stream::kSampleRate * 4, 4);
  word(32, 4, 2); word(34, 16, 2); text(36, "data"); word(40, Stream::kDataBytes, 4);
  return out;
}
// Counter hash gives independent uniform samples and reproducible arbitrary
// seeks (the MP3 detector and WAV parser both rewind). Limit source amplitude
// to +/- 1/8 full scale, then use the existing EQ/volume/protection chain.
uint16_t sample(uint32_t index) {
  uint32_t value = index + 0x9e3779b9U;
  value = (value ^ (value >> 16)) * 0x21f0aaadU;
  value = (value ^ (value >> 15)) * 0x735a2d97U;
  value ^= value >> 15;
  return static_cast<uint16_t>(static_cast<int>((value >> 19) & 8191) - 4096);
}
}

size_t Stream::read(char *data, size_t size) {
  if (data == nullptr) return 0;
  const auto wav = header();
  size = std::min<size_t>(size, kFileBytes - position_);
  for (size_t i = 0; i < size; ++i, ++position_) {
    if (position_ < wav.size()) data[i] = static_cast<char>(wav[position_]);
    else {
      const uint32_t offset = position_ - static_cast<uint32_t>(wav.size());
      data[i] = static_cast<char>(sample(offset / 2) >> ((offset & 1U) * 8));
    }
  }
  return size;
}

int64_t Stream::seek(int64_t offset, int origin) {
  if (origin != SEEK_SET && origin != SEEK_CUR && origin != SEEK_END) { errno = EINVAL; return -1; }
  const int64_t base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? position_ : kFileBytes;
  if (offset < -base || offset > int64_t(kFileBytes) - base) { errno = EINVAL; return -1; }
  position_ = static_cast<uint32_t>(base + offset);
  return position_;
}

#if defined(ESP_PLATFORM) || defined(__APPLE__)
namespace {
int read_stream(void *context, char *data, int size) {
  return size > 0 ? static_cast<int>(static_cast<Stream *>(context)->read(data, static_cast<size_t>(size))) : 0;
}
#ifdef ESP_PLATFORM
using SeekOffset = off_t;
#else
using SeekOffset = fpos_t;
#endif
SeekOffset seek_stream(void *context, SeekOffset offset, int origin) {
  return static_cast<SeekOffset>(static_cast<Stream *>(context)->seek(offset, origin));
}
int close_stream(void *context) { delete static_cast<Stream *>(context); return 0; }
}
FILE *open() {
  auto *stream = new (std::nothrow) Stream;
  if (stream == nullptr) return nullptr;
  FILE *file = funopen(stream, read_stream, nullptr, seek_stream, close_stream);
  if (file == nullptr) delete stream;
  return file;
}
#endif
}  // namespace smart_speaker::noise
