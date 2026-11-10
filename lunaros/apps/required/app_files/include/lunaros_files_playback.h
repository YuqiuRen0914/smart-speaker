#pragma once
#include "lunaros_files_formats.generated.h"
#include <cmath>
#include <cstdint>

namespace lunaredu::lunaros::media {
// The owner serializes calls on its managed task. No decoder, allocation,
// worker, filesystem access or implicit hardware capability is added here.
constexpr uint32_t format_bit(Format value) {
  return static_cast<uint8_t>(value) < static_cast<uint8_t>(Format::count)
    ? uint32_t{1} << static_cast<uint8_t>(value) : 0;
}
struct Capabilities {
  bool audio=false, video=false, audio_output=false;
  bool pause=false, seek=false, volume=false;
  uint32_t supported_formats=0;
  uint16_t max_video_width=0, max_video_height=0;
};
struct Request {
  const char *path=nullptr;
  Format format=Format::count;
  uint16_t width=0, height=0;
  bool has_audio=false;
};
enum class Result : uint8_t { ok, invalid, unavailable, unsupported_format,
  no_audio_output, resolution, stale, busy, backend_failed, unsupported_control };
Result probe(const Capabilities &c, const Request &r);
using Token=uint64_t;
enum class State : uint8_t { idle, ready, playing, paused, ended, error };
struct Snapshot { Token token=0; State state=State::idle; double position=0, duration=0; uint8_t volume=50; };
class Backend {
public:
  virtual ~Backend()=default;
  virtual Capabilities capabilities() const=0;
  // Backend owns/copies the path, validates its storage authorization, and
  // acquires AudioFocusBroker for audible playback. Video may be silent.
  virtual bool open(const Request &,Token,bool silent)=0;
  virtual bool play(Token)=0;
  virtual bool pause(Token)=0;
  virtual bool seek(Token,double seconds)=0;
  virtual bool volume(Token,uint8_t percent)=0;
  // Success guarantees DMA/render workers and audio leases have stopped.
  // Failure retains ownership; caller must retry, never silently hand off.
  virtual bool stop(Token)=0;
};
class Playback {
public:
  explicit Playback(Backend *backend=nullptr):backend_(backend) {}
  Playback(const Playback &)=delete;
  Playback &operator=(const Playback &)=delete;
  Capabilities capabilities() const { return backend_?backend_->capabilities():Capabilities{}; }
  Snapshot snapshot() const { return current_; }
  Result open(const Request &request);
  Result play(Token token);
  Result pause(Token token);
  Result seek(Token token,double seconds);
  Result volume(Token token,int percent);
  Result stop(Token token);
  bool report(Token token,State state,double position,double duration);
private:
  bool valid(Token token) const { return backend_ && token && token==current_.token && current_.state!=State::idle; }
  Result fail() { current_.state=State::error; return Result::backend_failed; }
  Backend *backend_=nullptr;
  Token next_=0;
  Snapshot current_{};
};
}
