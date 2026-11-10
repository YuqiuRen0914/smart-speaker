#pragma once
#include "smart_speaker_runtime.h"
#include "lunaredu_music_lyrics.h"
#include "lunaredu_music_playback_coordinator.h"
#include "sdkconfig.h"
#include <array>
#include <atomic>
#include <mutex>
#include <memory>
#include <freertos/FreeRTOS.h>
namespace smart_speaker {namespace lyrics=lunaredu::music::lyrics;}
namespace smart_speaker::detail {
constexpr int kInitialVolume =
    CONFIG_LUNAREDU_SMART_SPEAKER_STARTUP_VOLUME;
constexpr int kMaxVolume = CONFIG_LUNAREDU_SMART_SPEAKER_MAX_VOLUME;
using CommandType=lunaredu::music::PlaybackCommandType;
using Command=lunaredu::music::PlaybackCommand;

struct Runtime:lunaredu::music::PlaybackState {
  Runtime(){volume=kInitialVolume;}
  std::shared_ptr<const lyrics::Document> lyrics;
  int lyrics_delay_ms = 0;
  std::mutex mutex;
  std::vector<TrackInfo> playlist;
  bool sd_ready=false;

};
extern const lunaredu::lunaros::DeviceCapabilityDescriptor kCapability;
extern lunaredu::lunaros::DeviceProjectionBinding g_projection;
extern Runtime g_runtime;
extern std::mutex g_player_operation_mutex;
extern lunaredu::audio::AudioHistory g_audio_history;
bool enqueue_command(CommandType type, int value = 0, uint32_t playlist_revision = 0);
uint32_t now_ms();
void set_error_locked(const char *error);
void clear_error_locked();
bool ensure_music_dir();
// Caller holds the player-operation mutex; disk I/O never holds Runtime::mutex.
void rescan_library();
bool stop_music_locked(std::string &error);
void publish_imported_track(const std::string &path,size_t size);
bool play_index(int index);
}
