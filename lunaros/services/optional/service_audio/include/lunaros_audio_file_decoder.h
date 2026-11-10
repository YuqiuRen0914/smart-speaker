#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include "lunaros_audio_mp3.h"

namespace lunaredu::audio {
enum class AudioFileFormat { Wave, Mp3 };
// Streaming PCM16 decoder. Container parsing and codecs belong to Audio;
// callers own file access, audio focus, playback scheduling and cancellation.
// push accepts arbitrary chunk boundaries. No unbounded file allocation.
using DecodedAudioWriter = bool (*)(void *, const int16_t *, size_t frames,
                                    unsigned sample_rate, unsigned channels);
class AudioFileDecoder {
 public:
  AudioFileDecoder() = default;
  ~AudioFileDecoder();
  AudioFileDecoder(const AudioFileDecoder &) = delete;
  AudioFileDecoder &operator=(const AudioFileDecoder &) = delete;
  bool open(AudioFileFormat, DecodedAudioWriter, void *, std::string &);
  bool push(const uint8_t *, size_t, std::string &);
  bool finish(std::string &);
  void close(); // One stream per instance; releases the official decoder/registration.
  static bool supported(AudioFileFormat);
 private:
  bool process(bool end_of_stream,std::string &);
  bool fail(std::string &, const char *);
  AudioFileFormat format_ = AudioFileFormat::Wave;
  DecodedAudioWriter writer_ = nullptr;
  void *context_ = nullptr, *codec_ = nullptr;
  static constexpr uint64_t kMaximumFileBytes=64ULL*1024*1024;
  std::array<uint8_t,2048> input_{};
  std::array<int16_t,2304> pcm_{};
  size_t used_ = 0;
  std::array<uint8_t,12> header_{};
  Mp3StreamValidator mp3_validator_;
  size_t header_used_=0;
  uint64_t total_=0,expected_=0,frames_=0;
  bool header_checked_=false,registered_=false,finished_=false,failed_=false;
};
}
