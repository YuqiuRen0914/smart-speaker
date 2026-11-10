#include "product/speaker_desktop.h"
#include "device_api/device_operation_builders.h"
#include "device_api/device_api_context.h"
#include "lunaros_diagnostics_runtime.h"
#include "lunaredu_device_sdcard_storage.h"
#include "lunaredu_device_sdcard_tfcard_store.h"
#include "smart_speaker_audio.h"
#include "smart_speaker_sensors.h"
#include "runtime_internal.h"
#include "lyrics_api.h"
#include "transfer_internal.h"
#include "sdkconfig.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <iterator>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace smart_speaker {
using namespace lunaredu::lunaros;
using namespace detail;
namespace {
constexpr const char *kMusicDir="/sdcard/music";
constexpr const char *kModuleId="smart_speaker";
constexpr size_t kAudioDebugResponseFrameCount=24;

void fill_status(JsonObject payload, const RuntimeStatus &status);
void fill_settings(JsonObject payload, const dsp::Settings &settings);
void fill_audio_debug_frame(JsonObject item, const AudioDebugFrame &frame);

std::string request_arg(const DeviceBulkRequest &request, const char *name);
void fill_status(JsonObject payload, const RuntimeStatus &status) {
  payload["position_ms"] = status.position_ms;
  payload["has_lyrics"] = status.has_lyrics;
  payload["lyrics_delay_ms"] = status.lyrics_delay_ms;
  payload["lyric_line"] = status.lyric_line;
  payload["playback_mode"] = playback_mode_name(status.playback_mode);
  payload["playback_mode_label"] = playback_mode_label(status.playback_mode);
  payload["sd_ready"] = status.sd_ready;
  payload["audio_ready"] = status.audio_ready;
  payload["playing"] = status.playing;
  payload["paused"] = status.paused;
  payload["uploading"] = status.uploading;
  const auto transfer = transfer_snapshot();
  JsonObject upload = payload["upload"].to<JsonObject>();
  upload["receiving"] = transfer.receiving;
  upload["name"] = transfer.name;
  upload["received"] = transfer.received;
  upload["total"] = transfer.total;
  payload["volume"] = status.volume;
  payload["max_volume"] = kMaxVolume;
  payload["current_index"] = status.current_index;
  payload["current_name"] = status.current_name;
  payload["current_path"] = status.current_path;
  payload["playlist_count"] = status.playlist_count;
  payload["playlist_revision"] = status.playlist_revision;
  payload["last_error"] = status.last_error;
  payload["eq_preset"] = status.eq_preset;
  payload["protection_active"] = status.protection_active;
}

void fill_settings(JsonObject payload, const dsp::Settings &settings) {
  payload["preset"] = dsp::preset_name(settings.preset);
  payload["high_pass_enabled"] = settings.high_pass_enabled;
  payload["high_pass_hz"] = settings.high_pass_hz;
  payload["low_frequency_hz"] = settings.low_frequency_hz;
  payload["low_gain_db"] = settings.low_gain_db;
  payload["mid_frequency_hz"] = settings.mid_frequency_hz;
  payload["mid_q"] = settings.mid_q;
  payload["mid_gain_db"] = settings.mid_gain_db;
  payload["high_frequency_hz"] = settings.high_frequency_hz;
  payload["effective_high_frequency_hz"] =
      std::min(settings.high_frequency_hz,
               dual_i2s_diagnostics().sample_rate * 0.45f);
  payload["high_gain_db"] = settings.high_gain_db;
  payload["system_max_volume"] = kMaxVolume;
}

void fill_audio_debug_frame(JsonObject item, const AudioDebugFrame &frame) {
  item["seq"] = frame.sequence;
  item["ms"] = frame.timestamp_ms;
  item["volume"] = frame.volume;
  item["peak_in"] = frame.peak_in;
  item["peak_out"] = frame.peak_out;
  item["avg_abs_out"] = frame.avg_abs_out;
  item["limiter_gain"] = frame.limiter_gain_percent;
  item["limit"] = frame.limiter_events;
  item["clip"] = frame.soft_clip_events;
  item["i2s_err"] = frame.i2s_write_errors;
  item["i2s_short"] = frame.i2s_short_writes;
  item["gap_ms"] = frame.max_write_gap_ms;
  item["write_ms"] = frame.max_write_time_ms;
  item["dma_free"] = frame.dma_free_bytes;
  item["dma_largest"] = frame.dma_largest_block;
  item["dma_min_free"] = frame.dma_minimum_free_bytes;
}

std::string request_arg(const DeviceBulkRequest &request, const char *name) {
  return request.arg != nullptr ? request.arg(name, request.context)
                                : std::string();
}

constexpr DeviceOperationDefinition kOperations[] = {
    make_device_json_query("status.get", fill_status_payload),
    make_device_json_query("playlist.get", fill_playlist_payload),
    device_operation_diagnostic_access(make_device_json_query("debug.get", fill_debug_payload), DeviceDiagnosticAccess::Session),
    device_operation_diagnostic_access(make_device_json_query("display.stats.get", fill_display_stats_payload), DeviceDiagnosticAccess::Session),
    make_device_json_query("audio.settings.get", fill_audio_settings_payload),
    make_device_json_command("control", handle_control_payload),
    make_device_json_command("track.play", handle_track_play_payload),
    make_device_json_command("track.remove", handle_track_remove_payload),
    make_device_json_command("audio.settings.set", handle_audio_settings_payload),
    make_device_json_command("audio.test.set", handle_audio_test_payload),
    device_operation_diagnostic_access(make_device_json_command("audio.output.set", handle_audio_output_payload), DeviceDiagnosticAccess::Session),
    make_device_upload("lyrics.upload", handle_lyrics_upload, device_operation_on_executor(kNetworkIoServiceExecutor)),
    make_device_json_command("lyrics.remove", handle_lyrics_remove),
    make_device_json_command("lyrics.offset.set", handle_lyrics_offset),
    make_device_upload("file.upload", handle_file_upload, device_operation_on_executor(kNetworkIoServiceExecutor)),
};
const DeviceModuleProjectionMetadata kModule = {kModuleId, "media_player"};
const DeviceModuleProjection kSurface = {"smart_speaker", &kModule, kOperations, std::size(kOperations)};

}
bool register_projection(DeviceProjectionRegistry &registry, std::string &error) {
  return g_projection.declare(
      registry, {&kCapability, &kSurface, nullptr, false}, error);
}

DeviceOperationResult fill_status_payload(DeviceApiContext &, JsonObject payload) {
  fill_status(payload, status_snapshot());
  return device_operation_ack();
}

DeviceOperationResult fill_playlist_payload(DeviceApiContext &, JsonObject payload) {
  fill_status(payload["status"].to<JsonObject>(), status_snapshot());
  JsonArray items = payload["tracks"].to<JsonArray>();
  const auto tracks = playlist_snapshot();
  for (size_t index = 0; index < tracks.size(); ++index) {
    JsonObject item = items.add<JsonObject>();
    item["index"] = index;
    item["name"] = tracks[index].name;
    item["path"] = tracks[index].path;
    item["size"] = static_cast<uint64_t>(tracks[index].size_bytes);
    item["has_lyrics"] = tracks[index].has_lyrics;
    item["lyrics_delay_ms"] = tracks[index].lyrics_delay_ms;
  }
  return device_operation_ack();
}

DeviceOperationResult fill_debug_payload(DeviceApiContext &device, JsonObject payload) {
  write_task_runtime_counters(payload["cpu_runtime"].to<JsonObject>());
  std::array<AudioDebugFrame,kAudioDebugResponseFrameCount> frames;
  uint32_t latest_sequence=0;
  const auto frame_count=g_audio_history.read_recent(frames.data(),frames.size(),latest_sequence);
  payload["latest_seq"]=latest_sequence;
  JsonObject system_json=payload["system"].to<JsonObject>();
  write_system_runtime_diagnostics(system_json);
  system_json["startup_volume"] = kInitialVolume;
#if CONFIG_LUNAREDU_SMART_SPEAKER_AUTOPLAY_ON_BOOT
  system_json["autoplay_on_boot"] = true;
#else
  system_json["autoplay_on_boot"] = false;
#endif
  const dsp::Diagnostics dsp_status = dual_i2s_dsp_diagnostics();
  const DualI2sDiagnostics i2s = dual_i2s_diagnostics();
  JsonObject dsp_json = payload["dsp"].to<JsonObject>();
  dsp_json["input_peak"] = dsp_status.input_peak;
  dsp_json["output_peak"] = dsp_status.output_peak;
  dsp_json["rms"] = dsp_status.rms;
  dsp_json["eq_pre_gain"] = dsp_status.eq_pre_gain;
  dsp_json["limiter_gain"] = dsp_status.limiter_gain;
  dsp_json["protection_gain"] = dsp_status.protection_gain;
  dsp_json["limiter_events"] = dsp_status.limiter_events;
  dsp_json["limiter_samples"] = dsp_status.limiter_samples;
  dsp_json["continuous_limiter_ms"] = dsp_status.continuous_limiter_ms;
  dsp_json["max_limiter_reduction_db"] =
      dsp_status.maximum_limiter_reduction_db;
  dsp_json["soft_clip_samples"] = dsp_status.soft_clip_samples;
  dsp_json["invalid_samples"] = dsp_status.invalid_samples;
  JsonObject i2s_json = payload["dual_i2s"].to<JsonObject>();
  i2s_json["sample_rate"] = i2s.sample_rate;
  i2s_json["synchronized"] = i2s.synchronized;
  i2s_json["fault_muted"] = i2s.fault_muted;
  i2s_json["submitted_left"] = i2s.submitted_frames_left;
  i2s_json["submitted_right"] = i2s.submitted_frames_right;
  i2s_json["completed_left"] = i2s.completed_frames_left;
  i2s_json["completed_right"] = i2s.completed_frames_right;
  i2s_json["completion_delta"] = i2s.completion_counter_delta;
  i2s_json["completion_stall_left"] = i2s.completion_stalls_left;
  i2s_json["completion_stall_right"] = i2s.completion_stalls_right;
  i2s_json["short_left"] = i2s.short_writes_left;
  i2s_json["short_right"] = i2s.short_writes_right;
  i2s_json["timeout_left"] = i2s.timeouts_left;
  i2s_json["timeout_right"] = i2s.timeouts_right;
  i2s_json["send_q_ovf_left"] = i2s.send_queue_overflows_left;
  i2s_json["send_q_ovf_right"] = i2s.send_queue_overflows_right;
  i2s_json["underruns"] = i2s.inferred_underruns;
  i2s_json["input_q_ovf"] = i2s.input_queue_overflows;
  i2s_json["resyncs"] = i2s.resync_count;
  i2s_json["max_gap_ms"] = i2s.maximum_write_gap_ms;
  i2s_json["max_write_ms"] = i2s.maximum_write_time_ms;
  i2s_json["left_bclk_hz"] = i2s.left_bclk_hz;
  i2s_json["right_bclk_hz"] = i2s.right_bclk_hz;
  i2s_json["left_mclk_hz"] = i2s.left_mclk_hz;
  i2s_json["right_mclk_hz"] = i2s.right_mclk_hz;
  i2s_json["left_clock_source"] = i2s.left_clock_source;
  i2s_json["right_clock_source"] = i2s.right_clock_source;
  i2s_json["clocks_match"] = i2s.clocks_match;
  i2s_json["output_enabled"] = i2s.output_enabled;

  i2s_json["right_bclk_forced_low"] = i2s.right_bclk_forced_low;
  i2s_json["right_ws_forced_low"] = i2s.right_ws_forced_low;
  i2s_json["right_dout_forced_low"] = i2s.right_dout_forced_low;
  JsonObject display_json = payload["display"].to<JsonObject>();
  fill_desktop_status(device,display_json);
  const auto mpu = smart_speaker_mpu_snapshot();
  JsonObject mpu_json = payload["mpu6050"].to<JsonObject>();
  mpu_json["initialized"] = mpu.initialized;
  mpu_json["sampling"] = mpu.sampling;
  mpu_json["address"] = mpu.address;
  mpu_json["temperature_c"] = mpu.temperature_c;
  mpu_json["pose_valid"] = mpu.pose_valid;
  mpu_json["angle_x"] = mpu.angle_x;
  mpu_json["angle_y"] = mpu.angle_y;
  mpu_json["angle_z"] = mpu.angle_z;
  mpu_json["scan_complete"] = mpu.scan_complete;
  mpu_json["scan_count"] = mpu.scan_count;
  JsonArray mpu_scan = mpu_json["scan_addresses"].to<JsonArray>();
  const uint8_t mpu_scan_count = std::min<uint8_t>(
      mpu.scan_count, kSmartSpeakerI2cScanMaxAddresses);
  for (uint8_t index = 0; index < mpu_scan_count; ++index) {
    mpu_scan.add(mpu.scan_addresses[index]);
  }
  mpu_json["who_am_i_valid"] = mpu.who_am_i_valid;
  mpu_json["who_am_i"] = mpu.who_am_i;
  mpu_json["who_am_i_error"] = mpu.who_am_i_error;
  mpu_json["last_error"] = mpu.last_error;
  // Keep the diagnostic object discoverable without publishing fabricated Pet
  // motion. Raw MPU pose remains available above; no motion consumer is bound.
  JsonObject motion_json=payload["motion"].to<JsonObject>();
  motion_json["supported"]=false;motion_json["valid"]=false;
  for(const char *key:{"tilt_x","tilt_y","twist_z","angular_speed_dps"})motion_json[key]=nullptr;
  const auto touch = smart_speaker_touch_snapshot();
  JsonObject touch_json = payload["touch"].to<JsonObject>();
  touch_json["initialized"] = touch.initialized;
  touch_json["pressed"] = touch.pressed;
  touch_json["address"] = touch.address;
  touch_json["x"] = touch.x;
  touch_json["y"] = touch.y;
  touch_json["sample_count"] = touch.sample_count;
  touch_json["last_error"] = touch.last_error;
  payload["microphone"] = false;
  JsonArray items = payload["frames"].to<JsonArray>();
  for(size_t i=0;i<frame_count;++i)fill_audio_debug_frame(items.add<JsonObject>(),frames[i]);
  return device_operation_ack();
}

DeviceOperationResult fill_display_stats_payload(DeviceApiContext &device,JsonObject payload) {
  fill_desktop_status(device,payload);return device_operation_ack();
}

DeviceOperationResult handle_control_payload(DeviceApiContext &, JsonVariantConst request,
                                                JsonObject response) {
  if (status_snapshot().uploading) return device_operation_error("upload_in_progress", 409);
  const std::string action = request["action"] | "";
  if(action=="play"||action=="next"||action=="prev"||action=="previous"){
    std::string error;
    if(!enter_music(error))return device_operation_error(error.empty()?"music_foreground_unavailable":error.c_str(),409);
  }
  bool accepted=false;
  if (action == "play") {
    accepted=enqueue_command(CommandType::kPlay);
  } else if (action == "pause") {
    accepted=enqueue_command(CommandType::kPause);
  } else if (action == "stop") {
    accepted=enqueue_command(CommandType::kStop);
  } else if (action == "next") {
    accepted=enqueue_command(CommandType::kNext);
  } else if (action == "prev" || action == "previous") {
    accepted=enqueue_command(CommandType::kPrevious);
  } else if (action == "rescan") {
    accepted=enqueue_command(CommandType::kRescan);
  } else if (action == "cycle_mode") {
    accepted=enqueue_command(CommandType::kCycleMode);
  } else if (action == "volume") {
    accepted=enqueue_command(CommandType::kSetVolume,request["volume"] | kInitialVolume);
  } else {
    return device_operation_error("unknown_action", 400,
                                     "smart_speaker_control_failed");
  }
  if(!accepted)return device_operation_error("speaker_busy_or_queue_full",503);
  fill_status(response["status"].to<JsonObject>(), status_snapshot());
  return device_operation_ack();
}

DeviceOperationResult handle_track_play_payload(DeviceApiContext &, JsonVariantConst request,
                                                JsonObject response) {
  if (!request["path"].is<const char *>())
    return device_operation_error("invalid_track_path", 400);
  const std::string path = request["path"].as<std::string>();
  std::string error;
  if (!request_track_play(path, error)) {
    const uint16_t status = error == "track_not_found" ? 404
                            : error == "upload_in_progress" ? 409
                                                             : 503;
    return device_operation_error(error.c_str(), status);
  }
  response["accepted"] = true;
  response["path"] = path;
  return device_operation_ack();
}

DeviceOperationResult fill_audio_settings_payload(DeviceApiContext &, JsonObject payload) {
  fill_settings(payload, dual_i2s_audio_settings());
  payload["test_mode"] =
      dsp::channel_mode_name(dual_i2s_audio_channel_mode());
  return device_operation_ack();
}

DeviceOperationResult handle_audio_settings_payload(DeviceApiContext &, JsonVariantConst request,
                                                       JsonObject response) {
  dsp::Settings settings = dual_i2s_audio_settings();
  bool custom = false;
  if (!request["preset"].isNull()) {
    if (!request["preset"].is<const char *>())
      return device_operation_error("invalid_preset", 400,
                                       "smart_speaker_audio_settings_failed");
    dsp::Preset preset;
    if (!dsp::parse_preset(request["preset"] | "", preset) ||
        preset == dsp::Preset::kCustom)
      return device_operation_error("invalid_preset", 400,
                                       "smart_speaker_audio_settings_failed");
    settings = dsp::preset_settings(preset);
  }
#define APPLY_FLOAT(field, member)                                               \
  do {                                                                           \
    if (!request[field].isNull()) {                                              \
      if (!request[field].is<float>())                                           \
        return device_operation_error("invalid_numeric_type", 400,           \
                                         "smart_speaker_audio_settings_failed"); \
      settings.member = request[field].as<float>();                              \
      custom = true;                                                             \
    }                                                                            \
  } while (0)
  if (!request["high_pass_enabled"].isNull()) {
    if (!request["high_pass_enabled"].is<bool>())
      return device_operation_error("invalid_boolean_type", 400,
                                       "smart_speaker_audio_settings_failed");
    settings.high_pass_enabled = request["high_pass_enabled"].as<bool>();
    custom = true;
  }
  APPLY_FLOAT("high_pass_hz", high_pass_hz);
  APPLY_FLOAT("low_frequency_hz", low_frequency_hz);
  APPLY_FLOAT("low_gain_db", low_gain_db);
  APPLY_FLOAT("mid_frequency_hz", mid_frequency_hz);
  APPLY_FLOAT("mid_q", mid_q);
  APPLY_FLOAT("mid_gain_db", mid_gain_db);
  APPLY_FLOAT("high_frequency_hz", high_frequency_hz);
  APPLY_FLOAT("high_gain_db", high_gain_db);
#undef APPLY_FLOAT
  if (!dsp::validate_settings(settings))
    return device_operation_error("settings_out_of_range", 400,
                                     "smart_speaker_audio_settings_failed");
  if (custom) settings.preset = dsp::Preset::kCustom;
  dual_i2s_audio_set_settings(settings, true);
  fill_settings(response, settings);
  return device_operation_ack();
}

DeviceOperationResult handle_audio_test_payload(DeviceApiContext &, JsonVariantConst request,
                                                   JsonObject response) {
  dsp::ChannelMode mode;
  if (!request["mode"].is<const char *>() ||
      !dsp::parse_channel_mode(request["mode"] | "", mode))
    return device_operation_error("invalid_test_mode", 400,
                                     "smart_speaker_audio_test_failed");
  dual_i2s_audio_set_channel_mode(mode);
  response["mode"] = dsp::channel_mode_name(mode);
  return device_operation_ack();
}

DeviceOperationResult handle_audio_output_payload(DeviceApiContext &, JsonVariantConst request,
                                                     JsonObject response) {
  if (request["enabled"].isNull() || !request["enabled"].is<bool>())
    return device_operation_error("invalid_enabled", 400,
                                     "smart_speaker_audio_output_failed");
  const RuntimeStatus status = status_snapshot();
  if (status.playing || status.paused)
    return device_operation_error("stop_playback_first", 409,
                                     "smart_speaker_audio_output_failed");
  const bool enabled = request["enabled"].as<bool>();
  const bool has_bclk_isolation =
      !request["right_bclk_forced_low"].isNull();
  const bool has_ws_isolation = !request["right_ws_forced_low"].isNull();
  const bool has_dout_isolation =
      !request["right_dout_forced_low"].isNull();
  if (has_bclk_isolation || has_ws_isolation || has_dout_isolation) {
    if ((has_bclk_isolation &&
         !request["right_bclk_forced_low"].is<bool>()) ||
        (has_ws_isolation && !request["right_ws_forced_low"].is<bool>()) ||
        (has_dout_isolation &&
         !request["right_dout_forced_low"].is<bool>()))
      return device_operation_error("invalid_right_pin_isolation", 400,
                                       "smart_speaker_audio_output_failed");
    if (dual_i2s_diagnostics().output_enabled)
      return device_operation_error("disable_output_first", 409,
                                       "smart_speaker_audio_output_failed");
    const DualI2sDiagnostics isolation = dual_i2s_diagnostics();
    const bool bclk_forced_low = has_bclk_isolation
                                     ? request["right_bclk_forced_low"].as<bool>()
                                     : isolation.right_bclk_forced_low;
    const bool ws_forced_low = has_ws_isolation
                                   ? request["right_ws_forced_low"].as<bool>()
                                   : isolation.right_ws_forced_low;
    const bool dout_forced_low = has_dout_isolation
                                     ? request["right_dout_forced_low"].as<bool>()
                                     : isolation.right_dout_forced_low;
    if (!dual_i2s_audio_set_right_pin_isolation(
            bclk_forced_low, ws_forced_low, dout_forced_low))
      return device_operation_error("right_pin_transition_failed", 500,
                                       "smart_speaker_audio_output_failed");
  }
  if (!dual_i2s_audio_set_output_enabled(enabled, 500))
    return device_operation_error("output_transition_failed", 500,
                                     "smart_speaker_audio_output_failed");
  response["enabled"] = enabled;
  response["pins_forced_low"] = !enabled;
  const DualI2sDiagnostics isolation = dual_i2s_diagnostics();
  response["right_bclk_forced_low"] = isolation.right_bclk_forced_low;
  response["right_ws_forced_low"] = isolation.right_ws_forced_low;
  response["right_dout_forced_low"] = isolation.right_dout_forced_low;
  return device_operation_ack();
}

DeviceOperationResult handle_file_upload(DeviceApiContext &, const DeviceBulkRequest &request,
                                            JsonObject response) {
  std::unique_lock<std::mutex> player_lock(g_player_operation_mutex, std::try_to_lock);
  if (!player_lock.owns_lock()) return device_operation_error("speaker_busy", 409);
  const std::string name = request_arg(request, "name");
  if (!safe_upload_name(name)) {
    return device_operation_error("invalid_mp3_name", 400,
                                     "smart_speaker_upload_failed");
  }
  if (!lunaredu::device::tfcard_store_ready()) {
    return device_operation_error("sdcard_not_ready", 503,
                                     "smart_speaker_upload_failed");
  }
  if (request.content_length == 0 && request.body_size == 0) {
    return device_operation_error("empty_upload", 400,
                                     "smart_speaker_upload_failed");
  }
  if (std::max(request.content_length, request.body_size) > 32U * 1024U * 1024U)
    return device_operation_error("upload_too_large", 413);
  if (!ensure_music_dir()) return device_operation_error("music_directory_unavailable", 503);
  const std::string path = std::string(kMusicDir) + "/" + name;
  struct stat existing = {};
  if (stat(path.c_str(), &existing) == 0)
    return device_operation_error("track_already_exists", 409);
  const std::string temporary_path = path + ".upload";

  {
    std::lock_guard<std::mutex> lock(g_transfer_mutex);
    g_transfer = {};
    g_transfer.receiving = true;
    g_transfer.name = name;
    g_transfer.error.clear();
    g_transfer.received = 0;
    g_transfer.total = static_cast<uint32_t>(std::max(request.content_length, request.body_size));
  }
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.auto_advance = false;
    g_runtime.playing = false;
    g_runtime.paused = false;
    g_runtime.transfer_active = true;
    g_runtime.paused_by_volume = false;
    clear_error_locked();
  }
  std::string error;
  const uint32_t stop_started=now_ms();
  while(!stop_music_locked(error)&&now_ms()-stop_started<1500)vTaskDelay(pdMS_TO_TICKS(5));
  if(!error.empty()) {
    {std::lock_guard<std::mutex> lock(g_runtime.mutex);g_runtime.transfer_active=false;set_error_locked(error.c_str());}
    {std::lock_guard<std::mutex> lock(g_transfer_mutex);g_transfer.receiving=false;g_transfer.error=error;}
    return device_operation_error(error.c_str(),503);
  }

  size_t written = 0;
  bool ok = stream_upload_to_file(request, temporary_path, written,error);
  if (ok) ok = stat(path.c_str(), &existing) != 0 &&
               rename(temporary_path.c_str(), path.c_str()) == 0;

  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.transfer_active = false;
    if (!ok) {
      set_error_locked("upload_write_failed");
    }
  }
  if(ok)publish_imported_track(path,written);

  {
    std::lock_guard<std::mutex> lock(g_transfer_mutex);
    g_transfer.receiving = false;
    if(!ok)g_transfer.error=error.empty()?"upload_write_failed":error;
  }
  if (!ok) {
    (void)remove(temporary_path.c_str());
    const bool invalid=error=="audio_mp3_invalid"||error=="audio_file_truncated"||error=="audio_import_read_failed";
    return device_operation_error(error.empty()?"upload_write_failed":error.c_str(),invalid?400:500,
                                  "smart_speaker_upload_failed");
  }

  response["ok"] = true;
  response["name"] = name;
  response["path"] = path;
  response["size"] = static_cast<uint64_t>(written);
  fill_status(response["status"].to<JsonObject>(), status_snapshot());
  return device_operation_ack();
}
}
