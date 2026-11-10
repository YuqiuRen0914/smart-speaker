#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <ArduinoJson.h>
#include <esp_err.h>

#include "device_api/device_projection.h"
#include "lunaros_audio_dsp.h"
#include "lunaros_audio_history.h"
#include "smart_speaker_transfer.h"
#include "lunaredu_music_playback_mode.h"

namespace smart_speaker {
using lunaredu::music::PlaybackMode;
using lunaredu::music::TrackAdvance;
using lunaredu::music::advance_track;
using lunaredu::music::next_playback_mode;
using lunaredu::music::playback_mode_name;
using lunaredu::music::playback_mode_label;
bool start_audio_service(lunaredu::lunaros::KernelRuntime &runtime,
                         std::string &error);
bool stop_audio_service(lunaredu::lunaros::KernelRuntime &runtime,
                        std::string &error);
struct TrackInfo {
  std::string name;
  std::string path;
  size_t size_bytes = 0;
  bool has_lyrics = false;
  int lyrics_delay_ms = 0;
};

struct RuntimeStatus {
  uint32_t position_ms = 0;
  bool has_lyrics = false;
  int lyrics_delay_ms = 0;
  std::string lyric_line;
  PlaybackMode playback_mode = PlaybackMode::Sequential;
  uint32_t mode_sequence = 0;
  uint32_t mode_updated_ms = 0;
  bool sd_ready = false;
  bool audio_ready = false;
  bool playing = false;
  bool paused = false;
  bool uploading = false;
  int volume = 50;
  int current_index = -1;
  std::string current_name;
  std::string current_path;
  size_t playlist_count = 0;
  uint32_t playlist_revision = 0;
  std::string last_error;
  std::string eq_preset;
  bool protection_active = false;
};


TransferStatus transfer_snapshot();

using AudioDebugFrame=lunaredu::audio::AudioHistoryFrame;

struct DesktopInput { uint16_t buttons=0;int16_t encoder_position=0; };
DesktopInput desktop_input();
bool configure_music_backend(std::string &);
bool stop_music_checked(std::string &);
bool setup_runtime(lunaredu::lunaros::KernelRuntime &runtime);
void tick_runtime();
void poll_library_refresh();

RuntimeStatus status_snapshot();
std::vector<TrackInfo> playlist_snapshot();

void play();
void pause();
void stop();
void next();
void previous();
bool request_track_play(const std::string &path, std::string &error);
void set_volume(int volume);
void adjust_volume(int delta);
void rescan();
bool register_projection(lunaredu::lunaros::DeviceProjectionRegistry &registry,
                         std::string &error);

lunaredu::lunaros::DeviceOperationResult fill_status_payload(
    lunaredu::lunaros::DeviceApiContext &, JsonObject payload);
lunaredu::lunaros::DeviceOperationResult fill_playlist_payload(
    lunaredu::lunaros::DeviceApiContext &, JsonObject payload);
lunaredu::lunaros::DeviceOperationResult fill_debug_payload(
    lunaredu::lunaros::DeviceApiContext &, JsonObject payload);
lunaredu::lunaros::DeviceOperationResult fill_display_stats_payload(
    lunaredu::lunaros::DeviceApiContext &, JsonObject payload);
lunaredu::lunaros::DeviceOperationResult handle_control_payload(
    lunaredu::lunaros::DeviceApiContext &, JsonVariantConst request,
    JsonObject response);
lunaredu::lunaros::DeviceOperationResult handle_track_play_payload(
    lunaredu::lunaros::DeviceApiContext &, JsonVariantConst request,
    JsonObject response);
lunaredu::lunaros::DeviceOperationResult handle_track_remove_payload(
    lunaredu::lunaros::DeviceApiContext &, JsonVariantConst request,
    JsonObject response);
lunaredu::lunaros::DeviceOperationResult fill_audio_settings_payload(
    lunaredu::lunaros::DeviceApiContext &, JsonObject payload);
lunaredu::lunaros::DeviceOperationResult handle_audio_settings_payload(
    lunaredu::lunaros::DeviceApiContext &, JsonVariantConst request,
    JsonObject response);
lunaredu::lunaros::DeviceOperationResult handle_audio_test_payload(
    lunaredu::lunaros::DeviceApiContext &, JsonVariantConst request,
    JsonObject response);
lunaredu::lunaros::DeviceOperationResult handle_audio_output_payload(
    lunaredu::lunaros::DeviceApiContext &, JsonVariantConst request,
    JsonObject response);
lunaredu::lunaros::DeviceOperationResult handle_file_upload(
    lunaredu::lunaros::DeviceApiContext &,
    const lunaredu::lunaros::DeviceBulkRequest &request, JsonObject response);

} // namespace smart_speaker
