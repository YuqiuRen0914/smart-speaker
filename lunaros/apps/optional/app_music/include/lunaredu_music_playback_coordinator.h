#pragma once
#include "lunaredu_music_playback_mode.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace lunaredu::music {
enum class PlaybackCommandType : uint8_t {
  kPlay,kPlayTrack,kPause,kStop,kNext,kPrevious,kSetVolume,kAdjustVolume,
  kRescan,kAutoNext,kSkipFailed,kCycleMode
};
struct PlaybackCommand {
  PlaybackCommandType type=PlaybackCommandType::kPlay;
  int value=0;
  // Library revision for explicit selection; playback generation for completion.
  uint32_t playlist_revision=0;
};
enum class PlaybackEvent : uint8_t {Playing,Paused,Idle,Failed};

// One state, borrowed by the service-owned player and its product observations.
// Ordinary fields require the supplied state mutex. PCM flags are atomic so an
// audio callback never waits for filesystem/JSON observation locks.
struct PlaybackState {
  PlaybackMode playback_mode=PlaybackMode::Sequential;
  uint32_t mode_sequence=0,mode_updated_ms=0,playback_generation=0;
  bool advance_queued=false;
  unsigned failed_tracks=0;
  uint32_t playlist_revision=0;
  int current_index=-1,volume=50;
  bool audio_ready=false,playing=false,paused=false,paused_by_volume=false;
  bool transfer_active=false,auto_advance=true;
  std::atomic<bool> first_frame_seen{false},decode_timeout_skip_sent{false};
  uint32_t play_started_ms=0;
  std::string last_error;
};
struct PlaybackPolicy {
  int maximum_volume=100;
  bool pause_at_zero=false;
  uint32_t first_frame_timeout_ms=0,fade_timeout_ms=100;
};
struct PlaybackPorts {
  void *context=nullptr;
  // track_count is called with the state mutex held. Other callbacks are
  // invoked without it, except error notification (must not reenter state).
  size_t (*track_count)(void *)=nullptr;
  uint32_t (*now_ms)(void *)=nullptr;
  uint32_t (*entropy)(void *)=nullptr;
  bool (*enqueue)(void *,PlaybackCommand)=nullptr;
  bool (*play_index)(void *,int)=nullptr;
  bool (*pause)(void *)=nullptr;
  bool (*resume)(void *)=nullptr;
  bool (*stop)(void *)=nullptr;
  bool (*idle)(void *)=nullptr;
  void (*discard_pending)(void *)=nullptr;
  bool (*fade_to_mute)(void *,uint32_t)=nullptr;
  void (*fade_up)(void *)=nullptr;
  void (*set_volume)(void *,int)=nullptr;
  void (*refresh)(void *)=nullptr;
  bool (*persist_mode)(void *,PlaybackMode)=nullptr;
  void (*error)(void *,const char *)=nullptr;
};

// No worker, queue, library copy or output owner is created here. Bind complete
// ports for this object's lifetime. The existing command owner serializes
// execute/stop_checked/mode changes with file mutations and queue admission.
class PlaybackCoordinator {
public:
  PlaybackCoordinator(PlaybackState &,std::mutex &,PlaybackPolicy,PlaybackPorts);
  void execute(const PlaybackCommand &);
  void on_event(PlaybackEvent);
  void update_state(bool playing,bool paused,bool automatic);
  void first_frame();
  // Caller holds the state mutex and publishes track metadata in that same
  // transaction, before admitting the new FILE to the asynchronous decoder.
  void begin_track_locked(int index,uint32_t now_ms);
  void tick();
  // Caller closes command admission. The optional queue-discard port runs
  // after automatic advancement is disabled, before decoder stop admission.
  // Idle must mean FILE/PCM/focus retirement is physically acknowledged.
  bool stop_checked(std::string &error);
private:
  void complete(bool failed,bool idle_event=false);
  void cycle_mode();
  void error_locked(const char *);
  PlaybackState &state_;
  std::mutex &mutex_;
  PlaybackPolicy policy_;
  PlaybackPorts ports_;
  bool stop_requested_=false;
};
}
