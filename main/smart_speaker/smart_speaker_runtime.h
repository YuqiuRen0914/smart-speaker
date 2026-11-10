#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <ArduinoJson.h>
#include <esp_err.h>

#include "lunaredu_lunaros/device_api/device_projection.h"
#include "smart_speaker_dsp.h"
#include "smart_speaker_transfer.h"
#include "smart_speaker_playback_mode.h"

namespace smart_speaker {
bool start_audio_service(std::string &error);
bool stop_audio_service(std::string &error);
using namespace lunaredu::lunaros;

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
  std::string last_error;
  std::string eq_preset;
  bool protection_active = false;
};


TransferStatus transfer_snapshot();
bool dismiss_transfer();
bool transfer_dismissed(uint32_t generation);

struct AudioDebugFrame {
  uint32_t sequence = 0;
  uint32_t timestamp_ms = 0;
  int volume = 50;
  int peak_in = 0;
  int peak_out = 0;
  int avg_abs_out = 0;
  int limiter_gain_percent = 100;
  uint32_t limiter_events = 0;
  uint32_t soft_clip_events = 0;
  uint32_t i2s_write_errors = 0;
  uint32_t i2s_short_writes = 0;
  uint32_t max_write_gap_ms = 0;
  uint32_t max_write_time_ms = 0;
  uint32_t dma_free_bytes = 0;
  uint32_t dma_largest_block = 0;
  uint32_t dma_minimum_free_bytes = 0;
};

bool setup_runtime();
void tick_runtime();

RuntimeStatus status_snapshot();
std::vector<TrackInfo> playlist_snapshot();

void play();
void pause();
void stop();
void next();
void previous();
void set_volume(int volume);
void adjust_volume(int delta);
void rescan();
bool take_provisioning_request();
bool take_provisioning_cancel_request();
struct LocalMenuState {
  bool open = false;
  bool binding = false;
  int account_selected = 0;
  bool unbind_confirm = false;
  bool unbind_pending = false;
  std::string account_notice;
  bool information = false;
  bool network = false;
  int network_selected = 0;
  bool library = false;
  int library_index = 0;
  uint32_t library_revision = 0;
  std::string library_path;
  std::string library_notice;
  int information_page = 0;
  bool provisioning = false;
  bool provisioning_dismissed = false;
  int selected = 0;
  int hold_percent = 0;
  int mode_hold_percent = 0;
};
LocalMenuState local_menu_snapshot();
struct LocalLibraryView {
  std::vector<TrackInfo> tracks;
  int selected = 0;
  uint32_t revision = 0;
  bool sd_ready = false;
  bool uploading = false;
};
LocalLibraryView local_library_snapshot();
// Atomic visual event: low three bits are character::Action, upper bits sequence.
uint32_t visual_activity_snapshot();

bool register_projection(std::string &error);

DeviceOperationResult fill_status_payload(JsonObject payload);
DeviceOperationResult fill_playlist_payload(JsonObject payload);
DeviceOperationResult fill_debug_payload(JsonObject payload);
DeviceOperationResult fill_display_stats_payload(JsonObject payload);
DeviceOperationResult handle_logs_payload(JsonVariantConst request,
                                             JsonObject payload);
DeviceOperationResult fill_smart_ota_status_payload(JsonObject payload);
DeviceOperationResult handle_smart_ota_upload(const DeviceBulkRequest &request,
                                                 JsonObject response);
DeviceOperationResult handle_control_payload(JsonVariantConst request,
                                                JsonObject response);
DeviceOperationResult handle_track_play_payload(JsonVariantConst request,
                                                JsonObject response);
DeviceOperationResult handle_track_remove_payload(JsonVariantConst request,
                                                  JsonObject response);
DeviceOperationResult fill_audio_settings_payload(JsonObject payload);
DeviceOperationResult handle_audio_settings_payload(JsonVariantConst request,
                                                       JsonObject response);
DeviceOperationResult handle_audio_test_payload(JsonVariantConst request,
                                                   JsonObject response);
DeviceOperationResult handle_audio_output_payload(JsonVariantConst request,
                                                     JsonObject response);
DeviceOperationResult handle_file_upload(const DeviceBulkRequest &request,
                                            JsonObject response);

} // namespace smart_speaker
