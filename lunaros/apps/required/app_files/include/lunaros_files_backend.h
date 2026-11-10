#pragma once
#include "lunaros_files_playback.h"
#include "lunaros_files_model.h"
#include <array>
#include <vector>

namespace lunaredu::lunaros::media {
// The storage provider re-authorizes every range read; file size is bounded at
// admission. A JPEG sequence is contiguous JPEG frames, fixed 10 fps, no audio.
struct FileIo {
  void *context = nullptr;
  bool (*read)(void *, const char *, size_t, uint8_t *, size_t) = nullptr;
  bool (*jpeg)(void *, const uint8_t *, size_t, desktop::FilePixels &) = nullptr;
};
// Optional output port. start/stop must acquire/release AudioFocusBroker and
// join all pending I/O. completed() counts DAC-completed mono frames since the
// last start/flush, never merely submitted frames. Controller binds no port.
class FileAudioOutput {
public:
  virtual ~FileAudioOutput() = default;
  virtual bool ready() const = 0;
  virtual bool start(uint32_t sample_rate) = 0;
  virtual bool write(const int16_t *, size_t frames) = 0;
  virtual uint64_t completed() const = 0;
  virtual bool flush() = 0;
  virtual bool volume(uint8_t percent) = 0;
  virtual bool stop() = 0;
};
struct JpegSpan { uint32_t offset = 0, size = 0; };
class JpegSequence {
public:
  static constexpr size_t maximum_frames = 600, maximum_frame_bytes = 128 * 1024;
  bool feed(const uint8_t *, size_t);
  bool complete() const;
  const std::vector<JpegSpan> &frames() const { return frames_; }
private:
  enum class Parse { Prefix, Soi, MarkerPrefix, Marker, LengthHigh, LengthLow, Skip, Entropy, EntropyMarker };
  Parse state_ = Parse::Prefix;
  uint32_t offset_ = 0, start_ = 0, skip_ = 0;
  uint16_t length_ = 0;
  uint8_t marker_ = 0;
  bool failed_ = false;
  std::vector<JpegSpan> frames_;
  bool marker(uint8_t);
};
struct WaveInfo {
  uint32_t sample_rate = 0, data_offset = 0, data_bytes = 0;
  uint16_t channels = 0;
};
bool read_wave(FileIo io, const char *path, size_t bytes, WaveInfo &);
class FileBackend final : public Backend {
public:
  explicit FileBackend(FileIo io = {}, FileAudioOutput *audio = nullptr) : io_(io), audio_(audio) {}
  Capabilities capabilities() const override;
  void source_size(size_t size) { size_ = size; }
  bool open(const Request &, Token, bool silent) override;
  bool play(Token) override;
  bool pause(Token) override;
  bool seek(Token, double) override;
  bool volume(Token, uint8_t) override;
  bool stop(Token) override;
  bool tick(uint32_t now_ms); // <=4 KiB indexing, <=1 JPEG or <=512 PCM frames.
  Snapshot status() const { return state_; }
  bool loading() const { return indexing_; }
  std::shared_ptr<const desktop::FilePixels> image() const { return image_; }
private:
  FileIo io_;
  FileAudioOutput *audio_ = nullptr;
  std::string path_;
  size_t size_ = 0, scan_ = 0, submitted_ = 0, seek_frame_ = 0;
  Token token_ = 0;
  Format format_ = Format::count;
  Snapshot state_;
  WaveInfo wave_;
  // Runtime lives on the heap; keep bounded I/O scratch off the UI task stack.
  std::array<uint8_t,4096> scratch_{};
  std::array<int16_t,512> samples_{};
  JpegSequence sequence_;
  std::shared_ptr<const desktop::FilePixels> image_;
  uint32_t clock_ = 0;
  double clock_position_ = 0;
  bool indexing_ = false, audible_ = false, clock_ready_ = false;
  bool decode(size_t index);
  bool fail();
};
} // namespace lunaredu::lunaros::media
