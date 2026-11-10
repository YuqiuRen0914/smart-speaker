#pragma once
#include "smart_speaker_runtime.h"
#include "smart_speaker_lyrics_store.h"
#include "sdkconfig.h"
#include <array>
#include <mutex>
#include <memory>
#include <freertos/FreeRTOS.h>
namespace smart_speaker::detail {
constexpr int kInitialVolume =
    CONFIG_LUNAREDU_SMART_SPEAKER_STARTUP_VOLUME;
constexpr int kMaxVolume = CONFIG_LUNAREDU_SMART_SPEAKER_MAX_VOLUME;
constexpr size_t kAudioDebugFrameCount = 96;
enum class CommandType : uint8_t {
  kPlay,
  kPlayTrack,
  kPause,
  kStop,
  kNext,
  kPrevious,
  kSetVolume,
  kAdjustVolume,
  kRescan,
  kAutoNext,
  kSkipFailed,
  kCycleMode,
};

struct Command {
  CommandType type = CommandType::kPlay;
  int value = 0;
  uint32_t playlist_revision = 0;
};

struct Runtime {
  std::shared_ptr<const lyrics::Document> lyrics;
  int lyrics_delay_ms = 0;
  std::mutex mutex;
  PlaybackMode playback_mode = PlaybackMode::Sequential;
  uint32_t mode_sequence = 0, mode_updated_ms = 0;
  uint32_t playback_generation = 0;
  bool advance_queued = false;
  unsigned failed_tracks = 0;
  std::vector<TrackInfo> playlist;
  uint32_t playlist_revision = 0;
  int current_index = -1;
  int volume = kInitialVolume;
  bool sd_ready = false;
  bool audio_ready = false;
  bool playing = false;
  bool paused = false;
  bool paused_by_volume = false;
  bool uploading = false;
  bool auto_advance = true;
  bool first_frame_seen = false;
  bool decode_timeout_skip_sent = false;
  uint32_t play_started_ms = 0;
  std::string last_error;
};
extern const DeviceCapabilityDescriptor kCapability;
extern Runtime g_runtime;
extern std::mutex g_player_operation_mutex;
extern std::array<AudioDebugFrame,kAudioDebugFrameCount> g_audio_debug_frames;
extern portMUX_TYPE g_audio_debug_lock;
extern uint32_t g_audio_debug_sequence;
bool enqueue_command(CommandType type, int value = 0, uint32_t playlist_revision = 0);
uint32_t now_ms();
void set_error_locked(const char *error);
void clear_error_locked();
bool ensure_music_dir();
void rescan_locked();
bool play_index(int index);
}
