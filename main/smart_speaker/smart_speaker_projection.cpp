#include "smart_speaker_lyrics_store.h"
#include "smart_speaker_runtime.h"
#include "lunaredu_lunaros/delivery/resources.h"
#include "lunaredu_lunaros/device_link/device_link.h"
#include <ctime>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <dirent.h>
#include <limits>
#include <array>
#include <memory>
#include <mutex>
#include <sys/stat.h>
#include <vector>

#include <audio_player.h>
#include <driver/gpio.h>
#include <esp_check.h>
#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "lunaredu_lunaros/device_api/device_projection_json.h"
#include "lunaredu_lunaros/device_api/device_projection.h"
#include "lunaredu_lunaros/model/device_model.h"
#include "lunaredu_device_gpio_button.h"
#include "lunaredu_lunaros/desktop/local/lvgl_screen_runtime.h"
#include "lunaredu_device_sdcard_storage.h"
#include "lunaredu_device_sdcard_tfcard_store.h"
#include "lunaredu_lunaros/delivery/update.h"
#include "lunaredu_lunaros/network/network_commands.h"
#include "sdkconfig.h"
#include "smart_speaker_audio.h"
#include "smart_speaker_display.h"
#include "smart_speaker_character.h"
#include "smart_speaker_log_buffer.h"
#include "smart_speaker_sensors.h"
#include "smart_speaker_xiaozhi.h"

#include "runtime_internal.h"
#include "lyrics_api.h"
#include "cover_api.h"
#include "pet_frame_api.h"
#include "transfer_internal.h"
#include "product/product_state_runtime.h"
#include "product/desktop_resources.h"
#include "product/weather.h"
namespace smart_speaker {
using namespace detail;
namespace {
constexpr const char *kMusicDir="/sdcard/music";
constexpr const char *kModuleId="smart_speaker";
constexpr size_t kAudioDebugResponseFrameCount=24;
constexpr size_t kLogResponseMaxEntries=32;

uint32_t product_now_ms() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

uint32_t product_monotonic_seconds() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000000);
}

void fill_status(JsonObject payload, const RuntimeStatus &status);
void fill_settings(JsonObject payload, const dsp::Settings &settings);
void fill_smart_ota_status(JsonObject payload);
void fill_audio_debug_frame(JsonObject item, const AudioDebugFrame &frame);
DeviceOperationResult start_provision(JsonVariantConst, JsonObject);
DeviceOperationResult connect_saved(JsonVariantConst, JsonObject);

DeviceOperationResult finish_upload_batch(JsonVariantConst request, JsonObject);
DeviceOperationResult fill_product_payload(JsonObject);
DeviceOperationResult handle_product_navigation(JsonVariantConst, JsonObject);
DeviceOperationResult handle_product_desktop(JsonVariantConst, JsonObject);
DeviceOperationResult handle_product_pomodoro(JsonVariantConst, JsonObject);
DeviceOperationResult handle_product_reminder(JsonVariantConst, JsonObject);
DeviceOperationResult handle_product_computer(JsonVariantConst, JsonObject);
DeviceOperationResult handle_product_game(JsonVariantConst, JsonObject);
DeviceOperationResult handle_weather_configure(JsonVariantConst, JsonObject);
DeviceOperationResult handle_weather_refresh(JsonVariantConst, JsonObject);
DeviceOperationResult fill_wallpapers(JsonObject);
DeviceOperationResult handle_wallpaper_upload(const DeviceBulkRequest &, JsonObject);
DeviceOperationResult handle_wallpaper_select(JsonVariantConst, JsonObject);
DeviceOperationResult handle_wallpaper_remove(JsonVariantConst, JsonObject);
DeviceOperationResult handle_wallpaper_image(const DeviceMediaRequest &, const DeviceMediaWriter &);
const char *reset_reason_name(esp_reset_reason_t reason);
std::string request_arg(const DeviceBulkRequest &request, const char *name);
int read_ota_upload_chunk(uint8_t *buffer, size_t max_size, void *context);
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
  upload["generation"] = transfer.generation;
  upload["receiving"] = transfer.receiving;
  upload["batch_open"] = transfer.batch_open;
  upload["name"] = transfer.name;
  upload["received"] = transfer.received;
  upload["total"] = transfer.total;
  upload["completed"] = transfer.completed;
  upload["failed"] = transfer.failed;
  payload["volume"] = status.volume;
  payload["max_volume"] = kMaxVolume;
  payload["current_index"] = status.current_index;
  payload["current_name"] = status.current_name;
  payload["current_path"] = status.current_path;
  payload["source"] = status.white_noise ? "white_noise" : "music";
  payload["playlist_count"] = status.playlist_count;
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
  payload["spectrum_mode"] = settings.spectrum_pre_volume
                                  ? "pre_volume"
                                  : "actual_output";
  payload["system_max_volume"] = kMaxVolume;
}

void fill_smart_ota_status(JsonObject payload) {
  const OtaServiceStatus ota = ota_service_status_snapshot();
  const esp_app_desc_t *app = esp_app_get_description();
  payload["has_result"] = ota.has_result;
  payload["success"] = ota.success;
  payload["upload_active"] = ota.upload_active;
  payload["message"] = ota.message.c_str();
  payload["restart_pending"] = ota.restart_pending;
  payload["pending_verify"] = ota.pending_verify;
  payload["boot_confirmed"] = ota.boot_confirmed;
  payload["rollback_enabled"] = ota.rollback_enabled;
  payload["boot_confirm_remaining_ms"] = ota.boot_confirm_remaining_ms;
  payload["running_image_state"] = ota.running_image_state.c_str();
  if (app != nullptr) {
    payload["project_name"] = app->project_name;
    payload["version"] = app->version;
    payload["build_date"] = app->date;
    payload["build_time"] = app->time;
  }
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

DeviceOperationResult start_provision(JsonVariantConst, JsonObject) {
  std::string error;
  return request_network_start_provisioning(NetworkProvisioningMode::SoftAp, true, error)
      ? device_operation_ack() : device_operation_error(error.c_str(), 503);
}

DeviceOperationResult connect_saved(JsonVariantConst, JsonObject) {
  std::string error;
  return request_network_apply_saved_credential(0, error)
      ? device_operation_ack() : device_operation_error(error.c_str(), 503);
}

DeviceOperationResult finish_upload_batch(JsonVariantConst request, JsonObject) {
  std::lock_guard<std::mutex> lock(g_transfer_mutex);
  if (g_transfer.receiving) return device_operation_error("upload_in_progress", 409);
  if (g_transfer.batch_id == (request["batch_id"] | "")) {
    g_transfer.batch_open = false;
    g_transfer.updated_ms = now_ms();
  }
  return device_operation_ack();
}

const char *reset_reason_name(esp_reset_reason_t reason) {
  switch (reason) {
  case ESP_RST_POWERON: return "power_on";
  case ESP_RST_EXT: return "external";
  case ESP_RST_SW: return "software";
  case ESP_RST_PANIC: return "panic";
  case ESP_RST_INT_WDT: return "interrupt_watchdog";
  case ESP_RST_TASK_WDT: return "task_watchdog";
  case ESP_RST_WDT: return "watchdog";
  case ESP_RST_DEEPSLEEP: return "deep_sleep";
  case ESP_RST_BROWNOUT: return "brownout";
  case ESP_RST_SDIO: return "sdio";
  case ESP_RST_USB: return "usb";
  case ESP_RST_JTAG: return "jtag";
  case ESP_RST_EFUSE: return "efuse";
  case ESP_RST_PWR_GLITCH: return "power_glitch";
  case ESP_RST_CPU_LOCKUP: return "cpu_lockup";
  case ESP_RST_UNKNOWN:
  default: return "unknown";
  }
}

std::string request_arg(const DeviceBulkRequest &request, const char *name) {
  return request.arg != nullptr ? request.arg(name, request.context)
                                : std::string();
}

int read_ota_upload_chunk(uint8_t *buffer, size_t max_size, void *context) {
  DeviceBulkRequest *request = static_cast<DeviceBulkRequest *>(context);
  if (request == nullptr || request->read_chunk == nullptr) {
    return -1;
  }
  return request->read_chunk(buffer, max_size, request->context);
}
constexpr DeviceOperationDefinition kOperations[] = {
    make_device_json_command("provision.start", start_provision, device_operation_on_executor(kNetworkServiceExecutor)),
    make_device_json_command("provision.connect", connect_saved, device_operation_on_executor(kNetworkServiceExecutor)),
    make_device_json_query("status.get", fill_status_payload),
    make_device_json_query("playlist.get", fill_playlist_payload),
    make_device_json_query("debug.get", fill_debug_payload),
    make_device_json_query("display.stats.get", fill_display_stats_payload),
    make_device_json_query("ota.status.get", fill_smart_ota_status_payload),
    make_device_json_query("audio.settings.get", fill_audio_settings_payload),
    make_device_json_command("logs.get", handle_logs_payload),
    make_device_json_command("control", handle_control_payload),
    make_device_json_command("upload.batch.finish", finish_upload_batch),
    make_device_json_command("track.play", handle_track_play_payload),
    make_device_json_command("track.remove", handle_track_remove_payload),
    make_device_json_command("audio.settings.set", handle_audio_settings_payload),
    make_device_json_command("audio.test.set", handle_audio_test_payload),
    make_device_json_command("audio.output.set", handle_audio_output_payload),
    make_device_json_query("product.get", fill_product_payload),
    make_device_json_command("product.navigation", handle_product_navigation),
    make_device_json_command("product.desktop.set", handle_product_desktop, device_operation_on_executor(kNetworkIoServiceExecutor)),
    make_device_json_query("wallpaper.get", fill_wallpapers, kNetworkIoServiceExecutor),
    make_device_upload("wallpaper.upload", handle_wallpaper_upload, device_operation_on_executor(kNetworkIoServiceExecutor)),
    make_device_upload("pet.frame.upload", handle_pet_frame_upload, device_operation_on_executor(kNetworkIoServiceExecutor)),
    make_device_json_command("wallpaper.select", handle_wallpaper_select, device_operation_on_executor(kNetworkIoServiceExecutor)),
    make_device_json_command("wallpaper.remove", handle_wallpaper_remove, device_operation_on_executor(kNetworkIoServiceExecutor)),
    make_device_media("wallpaper.image", handle_wallpaper_image, kNetworkIoServiceExecutor),
    make_device_json_command("product.pomodoro", handle_product_pomodoro, device_operation_on_executor(kNetworkServiceExecutor)),
    make_device_json_command("product.reminder", handle_product_reminder, device_operation_on_executor(kNetworkServiceExecutor)),
    make_device_json_command("product.computer", handle_product_computer),
    make_device_json_command("product.game", handle_product_game),
    make_device_json_command("product.weather.configure", handle_weather_configure, device_operation_on_executor(kNetworkServiceExecutor)),
    make_device_json_command("product.weather.refresh", handle_weather_refresh),
    make_device_upload("lyrics.upload", handle_lyrics_upload, device_operation_on_executor(kNetworkIoServiceExecutor)),
    make_device_upload("cover.upload", handle_cover_upload, device_operation_on_executor(kNetworkIoServiceExecutor)),
    make_device_json_command("cover.remove", handle_cover_remove, device_operation_on_executor(kNetworkIoServiceExecutor)),
    make_device_media("cover.image", handle_cover_image, kNetworkIoServiceExecutor),
    make_device_json_command("lyrics.remove", handle_lyrics_remove),
    make_device_json_command("lyrics.offset.set", handle_lyrics_offset),
    make_device_upload("file.upload", handle_file_upload, device_operation_on_executor(kNetworkIoServiceExecutor)),
    make_device_upload("ota.firmware.upload", handle_smart_ota_upload, device_operation_on_executor(kNetworkIoServiceExecutor)),
};
const DeviceModuleProjectionMetadata kModule = {kModuleId, "media_player"};
const DeviceModuleProjection kSurface = {"smart_speaker", &kModule, kOperations, std::size(kOperations)};

}
bool register_projection(std::string &error) {
  return register_device_projection({&kCapability, &kSurface, nullptr, false}, &error);
}

DeviceOperationResult fill_status_payload(JsonObject payload) {
  fill_status(payload, status_snapshot());
  return device_operation_ack();
}

DeviceOperationResult fill_playlist_payload(JsonObject payload) {
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
    item["has_cover"] = tracks[index].has_cover;
    item["lyrics_delay_ms"] = tracks[index].lyrics_delay_ms;
  }
  return device_operation_ack();
}

DeviceOperationResult fill_debug_payload(JsonObject payload) {
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
  JsonObject cpu = payload["cpu_runtime"].to<JsonObject>();
  cpu["clock_mhz"] = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
  constexpr size_t capacity = 96;
  auto *tasks = static_cast<TaskStatus_t *>(heap_caps_calloc(capacity, sizeof(TaskStatus_t), MALLOC_CAP_SPIRAM));
  configRUN_TIME_COUNTER_TYPE total = 0;
  const UBaseType_t count = tasks ? uxTaskGetSystemState(tasks, capacity, &total) : 0;
  cpu["available"] = count != 0;
  cpu["counter_us"] = static_cast<uint64_t>(total);
  JsonArray entries = cpu["tasks"].to<JsonArray>();
  for (UBaseType_t i = 0; i < count; ++i) {
    JsonObject task = entries.add<JsonObject>();
    task["name"] = tasks[i].pcTaskName;
    task["runtime_us"] = static_cast<uint64_t>(tasks[i].ulRunTimeCounter);
  }
  heap_caps_free(tasks);
#endif
  std::array<AudioDebugFrame, kAudioDebugFrameCount> frames;
  uint32_t latest_sequence = 0;
  portENTER_CRITICAL(&g_audio_debug_lock);
  frames = g_audio_debug_frames;
  latest_sequence = g_audio_debug_sequence;
  portEXIT_CRITICAL(&g_audio_debug_lock);

  payload["latest_seq"] = latest_sequence;
  JsonObject system_json = payload["system"].to<JsonObject>();
  const esp_reset_reason_t reset_reason = esp_reset_reason();
  system_json["uptime_ms"] = now_ms();
  system_json["reset_reason"] = static_cast<int>(reset_reason);
  system_json["reset_reason_name"] = reset_reason_name(reset_reason);
  system_json["startup_volume"] = kInitialVolume;
  constexpr uint32_t kDmaCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
  system_json["dma_free_bytes"] = heap_caps_get_free_size(kDmaCaps);
  system_json["dma_largest_block"] =
      heap_caps_get_largest_free_block(kDmaCaps);
  system_json["dma_minimum_free_bytes"] =
      heap_caps_get_minimum_free_size(kDmaCaps);
  system_json["internal_free_bytes"] =
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  system_json["internal_largest_block"] =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
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
  i2s_json["microphone_input_enabled"] = i2s.microphone_input_enabled;
  i2s_json["right_bclk_forced_low"] = i2s.right_bclk_forced_low;
  i2s_json["right_ws_forced_low"] = i2s.right_ws_forced_low;
  i2s_json["right_dout_forced_low"] = i2s.right_dout_forced_low;
  const auto display = lunaredu::desktop::lvgl_screen_runtime_status();
  JsonObject display_json = payload["display"].to<JsonObject>();
  display_json["initialized"] = display.initialized;
  display_json["refresh_count"] = display.refresh_count;
  display_json["flush_count"] = display.flush_count;
  display_json["flush_fail_count"] = display.flush_fail_count;
  display_json["max_flush_us"] = display.max_flush_us;
  display_json["spectrum_generation"] = 0;
  display_json["spectrum_enabled"] = false;
  display_json["spectrum_dropped"] = 0;
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
  const auto motion = smart_speaker_motion_input();
  JsonObject motion_json = payload["motion"].to<JsonObject>();
  motion_json["valid"] = motion.valid;
  motion_json["tilt_x"] = motion.tilt_x;
  motion_json["tilt_y"] = motion.tilt_y;
  motion_json["twist_z"] = motion.twist_z;
  motion_json["angular_speed_dps"] = motion.angular_speed_dps;
  const auto touch = smart_speaker_touch_snapshot();
  JsonObject touch_json = payload["touch"].to<JsonObject>();
  touch_json["initialized"] = touch.initialized;
  touch_json["pressed"] = touch.pressed;
  touch_json["address"] = touch.address;
  touch_json["x"] = touch.x;
  touch_json["y"] = touch.y;
  touch_json["sample_count"] = touch.sample_count;
  touch_json["gesture_count"] = touch.gesture_count;
  touch_json["last_error"] = touch.last_error;
  payload["xiaozhi_status"] = xiaozhi_status_label();
  JsonArray items = payload["frames"].to<JsonArray>();
  const uint32_t response_count =
      static_cast<uint32_t>(kAudioDebugResponseFrameCount);
  const uint32_t first_sequence = latest_sequence > response_count
                                      ? latest_sequence - response_count + 1
                                      : 1;
  for (uint32_t sequence = first_sequence; sequence <= latest_sequence;
       ++sequence) {
    const AudioDebugFrame &frame =
        frames[sequence % kAudioDebugFrameCount];
    if (frame.sequence != sequence) {
      continue;
    }
    fill_audio_debug_frame(items.add<JsonObject>(), frame);
  }
  return device_operation_ack();
}

DeviceOperationResult fill_display_stats_payload(JsonObject payload) {
  const auto display = lunaredu::desktop::lvgl_screen_runtime_status();
  const auto speaker_display = smart_speaker_display_stats();
  payload["initialized"] = display.initialized;
  payload["refresh_count"] = display.refresh_count;
  payload["flush_count"] = display.flush_count;
  payload["flush_fail_count"] = display.flush_fail_count;
  payload["last_flush_us"] = display.last_flush_us;
  payload["max_flush_us"] = display.max_flush_us;
  payload["last_refresh_us"] = display.last_refresh_us;
  payload["max_refresh_us"] = display.max_refresh_us;
  payload["last_refresh_pixels"] = display.last_refresh_pixels;
  payload["full_refresh_count"] = display.full_refresh_count;
  payload["last_region_x"] = display.last_refresh_x;
  payload["last_region_y"] = display.last_refresh_y;
  payload["last_region_width"] = display.last_refresh_width;
  payload["last_region_height"] = display.last_refresh_height;
  payload["spectrum_render_count"] = speaker_display.spectrum_render_count;
  payload["spectrum_render_fail_count"] =
      speaker_display.spectrum_render_fail_count;
  payload["last_spectrum_render_us"] =
      speaker_display.last_spectrum_render_us;
  payload["max_spectrum_render_us"] =
      speaker_display.max_spectrum_render_us;
  payload["character_render_count"] = speaker_display.character_render_count;
  payload["character_render_fail_count"] = speaker_display.character_render_fail_count;
  payload["last_character_render_us"] = speaker_display.last_character_render_us;
  payload["max_character_render_us"] = speaker_display.max_character_render_us;
  payload["last_character_raster_us"] = speaker_display.last_character_raster_us;
  payload["last_character_transfer_us"] = speaker_display.last_character_transfer_us;
  payload["character_transferred_pixels"] = speaker_display.character_transferred_pixels;
  payload["last_character_pixels"] = speaker_display.last_character_pixels;
  payload["last_character_regions"] = speaker_display.last_character_regions;
  payload["progress_render_count"] = speaker_display.progress_render_count;
  payload["last_progress_render_us"] = speaker_display.last_progress_render_us;
  payload["character_over_budget_count"] = speaker_display.character_over_budget_count;
  payload["character_frame_ms"] = speaker_display.character_frame_ms;
  payload["character_visible"] = speaker_display.character_visible;
  payload["pet_animation_visible"] = speaker_display.pet_animation_visible;
  payload["display_stack_min_free_bytes"] = speaker_display.display_stack_min_free_bytes;
  payload["pet_animation_unavailable"] = speaker_display.pet_animation_unavailable;
  payload["pet_animation_frames_drawn"] = speaker_display.pet_animation_frames_drawn;
  payload["pet_animation_clip"] = speaker_display.pet_animation_clip;
  payload["pet_animation_frame"] = speaker_display.pet_animation_frame;
  payload["pet_dragging"] = speaker_display.pet_dragging;
  payload["boot_visible"] = speaker_display.boot_visible;
  payload["boot_frame_count"] = speaker_display.boot_frame_count;
  payload["boot_frame_mean_us"] = speaker_display.boot_frame_mean_us;
  payload["boot_frame_p95_us"] = speaker_display.boot_frame_p95_us;
  payload["boot_frame_max_us"] = speaker_display.boot_frame_max_us;
  payload["boot_frames_over_50ms"] = speaker_display.boot_frames_over_50ms;
  payload["boot_max_refresh_us"] = speaker_display.boot_max_refresh_us;
  payload["boot_refresh_count"] = speaker_display.boot_refresh_count;
  payload["boot_full_refresh_count"] = speaker_display.boot_full_refresh_count;
  payload["boot_flushed_pixels"] = speaker_display.boot_flushed_pixels;
  payload["lyrics_visible"] = speaker_display.lyrics_visible;
  payload["lyrics_layout"] = speaker_display.lyrics_layout;
  payload["transfer_page"] = speaker_display.transfer_page;
  payload["transfer_percent"] = speaker_display.transfer_percent;
  payload["volume_bar_visible"] = speaker_display.volume_bar_visible;
  payload["pause_indicator_visible"] = speaker_display.pause_indicator_visible;
  payload["pause_indicator_percent"] = speaker_display.pause_indicator_percent;
  payload["character_edge_contact"] = speaker_display.character_edge_contact;
  payload["character_edge_impact"] = speaker_display.character_edge_impact;
  payload["account_avatar_ready"] = speaker_display.account_avatar_ready;
  payload["account_avatar_generation"] = speaker_display.account_avatar_generation;
  payload["account_avatar_available"] = speaker_display.account_avatar_available;
  const auto avatar_resource = resource_snapshot("account-avatar");
  payload["avatar_resource_state"] = static_cast<int>(avatar_resource.state);
  payload["avatar_resource_error"] = avatar_resource.error;
  payload["lvgl_pool_free_bytes"] = speaker_display.lvgl_pool_free_bytes;
  payload["lvgl_pool_largest_block"] = speaker_display.lvgl_pool_largest_block;
  const auto menu = local_menu_snapshot();
  payload["account_selected"] = menu.account_selected;
  payload["account_unbind_confirm"] = menu.unbind_confirm;
  payload["account_unbind_pending"] = menu.unbind_pending;
  payload["account_notice"] = menu.account_notice;
  payload["local_page"] = speaker_display.boot_visible ? "boot" : menu.product_shell ? "product_shell" : menu.library ? "library" : menu.network ? "network" : menu.provisioning ? "provisioning" : menu.information ? "device_info" :
                          menu.binding ? "account" : menu.open ? "settings" : "home";
  payload["information_page"] = menu.information_page;
  payload["library_index"] = menu.library_index;
  payload["network_selected"] = menu.network_selected;
  payload["character_mood"] = character::mood_name(
      static_cast<character::Mood>(speaker_display.character_mood));
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
  const auto motion = smart_speaker_motion_input();
  JsonObject motion_json = payload["motion"].to<JsonObject>();
  motion_json["valid"] = motion.valid;
  motion_json["tilt_x"] = motion.tilt_x;
  motion_json["tilt_y"] = motion.tilt_y;
  motion_json["twist_z"] = motion.twist_z;
  motion_json["angular_speed_dps"] = motion.angular_speed_dps;
  const auto touch = smart_speaker_touch_snapshot();
  JsonObject touch_json = payload["touch"].to<JsonObject>();
  touch_json["initialized"] = touch.initialized;
  touch_json["pressed"] = touch.pressed;
  touch_json["address"] = touch.address;
  touch_json["x"] = touch.x;
  touch_json["y"] = touch.y;
  touch_json["sample_count"] = touch.sample_count;
  touch_json["gesture_count"] = touch.gesture_count;
  touch_json["last_error"] = touch.last_error;
  payload["xiaozhi_status"] = xiaozhi_status_label();
  payload["spectrum_generation"] = 0;
  payload["spectrum_enabled"] = false;
  payload["spectrum_dropped"] = 0;
  payload["target_frame_ms"] = 20;
  payload["spi_clock_hz"] =
      CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_PIXEL_CLOCK_HZ;
  return device_operation_ack();
}

DeviceOperationResult handle_logs_payload(JsonVariantConst request,
                                             JsonObject payload) {
  uint32_t after_sequence = 0;
  size_t limit = kLogResponseMaxEntries;
  if (!request.isNull()) {
    if (!request["after_seq"].isNull()) {
      if (!request["after_seq"].is<uint32_t>())
        return device_operation_error("invalid_after_seq", 400,
                                         "smart_speaker_logs_invalid");
      after_sequence = request["after_seq"].as<uint32_t>();
    }
    if (!request["limit"].isNull()) {
      if (!request["limit"].is<unsigned int>())
        return device_operation_error("invalid_limit", 400,
                                         "smart_speaker_logs_invalid");
      limit = std::clamp<size_t>(request["limit"].as<unsigned int>(), 1,
                                 kLogResponseMaxEntries);
    }
  }

  const LogBufferStatus status = log_buffer_status();
  payload["oldest_seq"] = status.oldest_sequence;
  payload["latest_seq"] = status.latest_sequence;
  payload["capacity"] = kLogBufferCapacity;
  payload["max_limit"] = kLogResponseMaxEntries;
  payload["count"] = status.count;
  payload["overwritten"] = status.overwritten_entries;
  payload["truncated"] = status.truncated_entries;
  payload["application_logs_only"] = true;
  payload["gap"] = status.count != 0 && after_sequence != 0 &&
                   after_sequence < UINT32_MAX &&
                   after_sequence + 1 < status.oldest_sequence;

  uint32_t first = status.oldest_sequence;
  if (after_sequence != 0 && after_sequence < UINT32_MAX)
    first = std::max(first, after_sequence + 1);
  JsonArray entries = payload["entries"].to<JsonArray>();
  size_t emitted = 0;
  for (uint32_t sequence = first;
       sequence != 0 && sequence <= status.latest_sequence && emitted < limit;
       ++sequence) {
    BufferedLogEntry entry;
    if (!log_buffer_read(sequence, &entry)) continue;
    JsonObject item = entries.add<JsonObject>();
    item["seq"] = entry.sequence;
    item["ms"] = entry.timestamp_ms;
    item["text"] = entry.text;
    ++emitted;
  }
  return device_operation_ack();
}

DeviceOperationResult fill_smart_ota_status_payload(JsonObject payload) {
  fill_smart_ota_status(payload);
  return device_operation_ack();
}

DeviceOperationResult handle_smart_ota_upload(const DeviceBulkRequest &request,
                                                 JsonObject response) {
  DeviceBulkRequest request_copy = request;
  OtaFirmwareStream stream;
  stream.content_length = request.content_length;
  stream.context = &request_copy;
  stream.read_chunk = read_ota_upload_chunk;

  if (!ota_service_install_stream(stream)) {
    fill_smart_ota_status(response);
    const OtaServiceStatus status = ota_service_status_snapshot();
    return device_operation_error(
        status.message.empty() ? "ota_install_failed" : status.message.c_str(),
        status.message == "empty_ota_stream" ? 400 : 500,
        "smart_speaker_ota_failed");
  }

  fill_smart_ota_status(response);
  return device_operation_ack();
}

DeviceOperationResult handle_control_payload(JsonVariantConst request,
                                                JsonObject response) {
  if (status_snapshot().uploading) return device_operation_error("upload_in_progress", 409);
  const std::string action = request["action"] | "";
  bool accepted = true;
  if (action == "play") {
    accepted = enqueue_command(CommandType::kPlay);
  } else if (action == "white_noise") {
    accepted = enqueue_command(CommandType::kWhiteNoise);
  } else if (action == "pause") {
    accepted = enqueue_command(CommandType::kPause);
  } else if (action == "stop") {
    accepted = enqueue_command(CommandType::kStop);
  } else if (action == "next") {
    accepted = enqueue_command(CommandType::kNext);
  } else if (action == "prev" || action == "previous") {
    accepted = enqueue_command(CommandType::kPrevious);
  } else if (action == "rescan") {
    rescan();
  } else if (action == "cycle_mode") {
    if (!enqueue_command(CommandType::kCycleMode))
      return device_operation_error("command_queue_full", 503);
  } else if (action == "volume") {
    const int volume = request["volume"] | kInitialVolume;
    if (volume < 0 || volume > kMaxVolume)
      return device_operation_error("volume_out_of_range", 400);
    accepted = enqueue_command(CommandType::kSetVolume, volume);
  } else {
    return device_operation_error("unknown_action", 400,
                                     "smart_speaker_control_failed");
  }
  if (!accepted) return device_operation_error("command_queue_full", 503,
                                                "smart_speaker_control_pending");
  response["accepted"] = true;
  response["execution"] = "queued";
  response["message"] = "命令已入队，执行结果请通过状态接口确认";
  fill_status(response["status"].to<JsonObject>(), status_snapshot());
  return device_operation_ack();
}

DeviceOperationResult handle_track_play_payload(JsonVariantConst request,
                                                JsonObject response) {
  if (!request["path"].is<const char *>())
    return device_operation_error("invalid_track_path", 400);
  const std::string path = request["path"].as<std::string>();
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (g_runtime.uploading) return device_operation_error("upload_in_progress", 409);
    if (!g_runtime.audio_ready) return device_operation_error("audio_not_ready", 503);
    const auto track = std::find_if(g_runtime.playlist.begin(), g_runtime.playlist.end(),
        [&path](const TrackInfo &item) { return item.path == path; });
    if (track == g_runtime.playlist.end()) return device_operation_error("track_not_found", 404);
    if (!enqueue_command(CommandType::kPlayTrack,
                         static_cast<int>(std::distance(g_runtime.playlist.begin(), track)),
                         g_runtime.playlist_revision))
      return device_operation_error("command_queue_full", 503);
  }
  response["accepted"] = true;
  response["execution"] = "queued";
  response["path"] = path;
  return device_operation_ack();
}

DeviceOperationResult fill_audio_settings_payload(JsonObject payload) {
  fill_settings(payload, dual_i2s_audio_settings());
  payload["test_mode"] =
      dsp::channel_mode_name(dual_i2s_audio_channel_mode());
  return device_operation_ack();
}

DeviceOperationResult handle_audio_settings_payload(JsonVariantConst request,
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
  if (!request["spectrum_mode"].isNull()) {
    if (!request["spectrum_mode"].is<const char *>())
      return device_operation_error("invalid_spectrum_mode", 400,
                                       "smart_speaker_audio_settings_failed");
    const std::string mode = request["spectrum_mode"] | "";
    if (mode != "actual_output" && mode != "pre_volume")
      return device_operation_error("invalid_spectrum_mode", 400,
                                       "smart_speaker_audio_settings_failed");
    settings.spectrum_pre_volume = mode == "pre_volume";
    custom = true;
  }
  if (!dsp::validate_settings(settings))
    return device_operation_error("settings_out_of_range", 400,
                                     "smart_speaker_audio_settings_failed");
  if (custom) settings.preset = dsp::Preset::kCustom;
  dual_i2s_audio_set_settings(settings, true);
  fill_settings(response, settings);
  return device_operation_ack();
}

DeviceOperationResult handle_audio_test_payload(JsonVariantConst request,
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

DeviceOperationResult handle_audio_output_payload(JsonVariantConst request,
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

DeviceOperationResult handle_file_upload(const DeviceBulkRequest &request,
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
    std::string batch = request_arg(request, "batch_id");
    if (batch.size() > 64) batch.clear();
    if (batch.empty() || batch != g_transfer.batch_id || !g_transfer.batch_open) {
      const uint32_t generation = g_transfer.generation + 1;
      g_transfer = {};
      g_transfer.generation = generation;
    }
    g_transfer.batch_id = batch;
    g_transfer.batch_open = !batch.empty();
    g_transfer.receiving = true;
    g_transfer.name = name;
    g_transfer.error.clear();
    g_transfer.received = 0;
    g_transfer.total = static_cast<uint32_t>(std::max(request.content_length, request.body_size));
    g_transfer.updated_ms = now_ms();
  }
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.auto_advance = false;
    g_runtime.playing = false;
    g_runtime.paused = false;
    g_runtime.uploading = true;
    g_runtime.paused_by_volume = false;
    clear_error_locked();
  }
  (void)dual_i2s_audio_fade_to_mute(100);
  (void)audio_player_stop();
  vTaskDelay(pdMS_TO_TICKS(50));

  size_t written = 0;
  bool ok = stream_upload_to_file(request, temporary_path, written);
  if (ok) ok = stat(path.c_str(), &existing) != 0 &&
               rename(temporary_path.c_str(), path.c_str()) == 0;

  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.uploading = false;
    if (!ok) {
      set_error_locked("upload_write_failed");
    } else {
      rescan_locked();
    }
  }

  {
    std::lock_guard<std::mutex> lock(g_transfer_mutex);
    g_transfer.receiving = false;
    g_transfer.updated_ms = now_ms();
    if (ok) ++g_transfer.completed;
    else { ++g_transfer.failed; g_transfer.error = "upload_write_failed"; }
  }
  if (!ok) {
    (void)remove(temporary_path.c_str());
    return device_operation_error("upload_write_failed", 500,
                                     "smart_speaker_upload_failed");
  }

  response["ok"] = true;
  response["name"] = name;
  response["path"] = path;
  response["size"] = static_cast<uint64_t>(written);
  fill_status(response["status"].to<JsonObject>(), status_snapshot());
  return device_operation_ack();
}

namespace {
DeviceOperationResult fill_product_payload(JsonObject payload) {
  const auto state = product::product_state_snapshot(product_monotonic_seconds(), product_now_ms());
  JsonObject navigation = payload["navigation"].to<JsonObject>();
  navigation["desktop"] = "wallpaper";
  navigation["page"] = product::page_name(state.navigation.page);
  navigation["module"] = product::module_name(state.navigation.module);
  navigation["item"] = state.navigation.item;
  navigation["revision"] = state.navigation.revision;
  const auto desktop_resources = product::desktop_resources_snapshot();
  JsonObject desktop = payload["desktop_resources"].to<JsonObject>();
  desktop["selected"] = desktop_resources.selected;
  desktop["ready"] = desktop_resources.pixels != nullptr;
  desktop["error"] = desktop_resources.error;
  JsonObject pomodoro = payload["pomodoro"].to<JsonObject>();
  pomodoro["active"] = state.pomodoro.active;
  pomodoro["paused"] = state.pomodoro.paused;
  pomodoro["round"] = state.pomodoro.round;
  pomodoro["remaining_seconds"] = state.pomodoro.remaining_seconds;
  pomodoro["revision"] = state.pomodoro.revision;
  payload["time_storage_ready"] = state.time_storage_ready;
  payload["time_storage_error"] = state.time_storage_error;
  payload["time_operation_pending"] = state.time_operation_pending;
  JsonArray reminders = payload["reminders"].to<JsonArray>();
  for (const auto &reminder : state.reminders) {
    JsonObject item = reminders.add<JsonObject>();
    item["id"] = reminder.id;
    item["due_epoch_seconds"] = reminder.due_epoch_seconds;
    item["repeat"] = reminder.repeat == product::ReminderRepeat::Weekly ? "weekly" : "once";
    item["state"] = reminder.state == product::ReminderState::Scheduled ? "scheduled" :
                     reminder.state == product::ReminderState::Pending ? "pending" :
                     reminder.state == product::ReminderState::Missed ? "missed" : "handled";
    item["title"] = reminder.title;
  }
  JsonArray pending = payload["pending_reminders"].to<JsonArray>();
  for (const auto event : state.pending_reminders) {
    JsonObject item = pending.add<JsonObject>();
    item["id"] = event.id;
    item["missed"] = event.missed;
  }
  JsonObject computer = payload["computer"].to<JsonObject>();
  computer["paired_id"] = state.paired_computer;
  computer["cpu_status"] = product::telemetry_status_name(state.computer.cpu);
  computer["gpu_status"] = product::telemetry_status_name(state.computer.gpu);
  computer["temperature_status"] = product::telemetry_status_name(state.computer.temperature);
  computer["fan_status"] = product::telemetry_status_name(state.computer.fan);
  if (state.computer.cpu == product::TelemetryStatus::Fresh) computer["cpu_percent"] = state.computer.cpu_percent;
  else computer["cpu_percent"] = nullptr;
  if (state.computer.gpu == product::TelemetryStatus::Fresh) computer["gpu_percent"] = state.computer.gpu_percent;
  else computer["gpu_percent"] = nullptr;
  if (state.computer.temperature == product::TelemetryStatus::Fresh) computer["temperature_c"] = state.computer.temperature_c;
  else computer["temperature_c"] = nullptr;
  if (state.computer.fan == product::TelemetryStatus::Fresh) computer["fan_rpm"] = state.computer.fan_rpm;
  else computer["fan_rpm"] = nullptr;
  computer["sample_age_ms"] = state.computer.sample_age_ms;
  computer["revision"] = state.computer.revision;
  JsonObject games = payload["games"].to<JsonObject>();
  games["wood_fish_count"] = state.wood_fish.count;
  games["wood_fish_last_hit_ms"] = state.wood_fish.last_hit_ms;
  games["score_2048"] = state.game_score;
  JsonArray board = games["board_2048"].to<JsonArray>();
  for (const auto value : state.game_board) board.add(value);
  JsonObject information = payload["information"].to<JsonObject>();
  const bool clock_valid = network_snapshot().clock == NetworkClockState::Valid;
  const auto network = network_snapshot();
  const auto weather = product::weather_snapshot(product_now_ms(), std::time(nullptr), clock_valid,
      network.link == NetworkLinkState::Up);
  information["clock_valid"] = clock_valid;
  information["weather_status"] = weather.status;
  information["weather_error"] = weather.error;
  information["weather_place"] = weather.config.place;
  information["weather_enabled"] = weather.config.enabled;
  information["weather_latitude"] = weather.config.latitude;
  information["weather_longitude"] = weather.config.longitude;
  information["weather_age_seconds"] = weather.age_seconds;
  information["weather_refreshing"] = weather.refreshing;
  information["weather_retry_required"] = weather.retry_required;
  if (weather.has_reading) {
    information["temperature_c"] = weather.reading.temperature_c;
    information["humidity_percent"] = weather.reading.humidity_percent;
    information["weather_code"] = weather.reading.code;
    information["weather_label"] = product::weather_code_label(weather.reading.code);
    information["weather_observed_epoch"] = weather.reading.observed_epoch;
  }
  if (clock_valid) {
    const std::time_t now = std::time(nullptr);
    product::MonthlyCalendar calendar;
    if (product::make_local_calendar(now, true, state.reminders, state.pending_reminders, calendar)) {
        information["year"] = calendar.year; information["month"] = calendar.month; information["day"] = calendar.today;
        JsonArray cells = information["days"].to<JsonArray>();
        for (const auto &cell : calendar.days) {
          JsonObject item = cells.add<JsonObject>(); item["day"] = cell.day;
          item["in_month"] = cell.in_month; item["today"] = cell.today; item["reminder"] = cell.has_reminder;
        }
    }
  }
  return device_operation_ack();
}

DeviceOperationResult handle_product_navigation(JsonVariantConst request, JsonObject response) {
  const std::string action = request["action"] | "";
  const product::Input input = action == "up" ? product::Input::Up : action == "down" ? product::Input::Down :
      action == "confirm" ? product::Input::Confirm : action == "back" ? product::Input::Back :
      action == "home" ? product::Input::Home : action == "assistant" ? product::Input::Assistant : product::Input::Home;
  if (action != "up" && action != "down" && action != "confirm" && action != "back" && action != "home" && action != "assistant")
    return device_operation_error("invalid_navigation_action", 400);
  if (!product::product_navigation_input(input)) return device_operation_error("navigation_unchanged", 409);
  return fill_product_payload(response);
}

DeviceOperationResult handle_product_desktop(JsonVariantConst request, JsonObject response) {
  const std::string mode = request["mode"] | "";
  if (mode != "wallpaper") return device_operation_error("invalid_desktop_mode", 400);
  return fill_product_payload(response);
}

DeviceOperationResult fill_wallpapers(JsonObject response) {
  std::vector<std::string> names;
  std::string error;
  if (!product::wallpaper_list(names, error)) return device_operation_error(error.c_str(), 503);
  auto items = response["files"].to<JsonArray>();
  for (const auto &name : names) items.add(name);
  const auto state = product::desktop_resources_snapshot();
  response["selected"] = state.selected;
  response["ready"] = state.pixels != nullptr;
  response["error"] = state.error;
  return device_operation_ack();
}

DeviceOperationResult handle_wallpaper_upload(const DeviceBulkRequest &request, JsonObject response) {
  const auto name = request_arg(request, "name");
  if (!product::wallpaper_name_valid(name)) return device_operation_error("invalid_wallpaper_name", 400);
  const size_t size = std::max(request.body_size, request.content_length);
  if (size != product::kWallpaperBmpBytes) return device_operation_error("wallpaper_size_invalid", 413);
  product::WallpaperBmp bmp;
  if (!bmp.resize(size)) return device_operation_error("wallpaper_memory_unavailable", 503);
  if (request.body_data && request.body_size == size) std::memcpy(bmp.data(), request.body_data, size);
  else if (request.read_chunk && request.content_length == size) {
    size_t received = 0;
    while (received < size) {
      const size_t wanted = std::min<size_t>(2048, size - received);
      const int got = request.read_chunk(bmp.data() + received, wanted, request.context);
      if (got <= 0 || static_cast<size_t>(got) > wanted) return device_operation_error("wallpaper_read_failed", 400);
      received += static_cast<size_t>(got);
    }
  } else return device_operation_error("wallpaper_read_failed", 400);
  std::string error;
  if (!product::wallpaper_upload(name, bmp, error)) return device_operation_error(error.c_str(), 409);
  response["name"] = name;
  response["stored"] = true;
  response["selected"] = false;
  return device_operation_ack();
}

DeviceOperationResult handle_wallpaper_select(JsonVariantConst request, JsonObject response) {
  const std::string name = request["name"] | "";
  std::string error;
  if (!product::wallpaper_select(name, error)) return device_operation_error(error.c_str(), 409);
  return fill_wallpapers(response);
}
DeviceOperationResult handle_wallpaper_remove(JsonVariantConst request, JsonObject response) {
  const std::string name = request["name"] | "";
  std::string error;
  if (!product::wallpaper_remove(name, error)) return device_operation_error(error.c_str(), 409);
  return fill_wallpapers(response);
}
DeviceOperationResult handle_wallpaper_image(const DeviceMediaRequest &request, const DeviceMediaWriter &writer) {
  product::WallpaperBmp bmp;
  std::string error;
  if (!product::wallpaper_read(request.path, bmp, error)) return device_operation_error(error.c_str(), 404);
  if (!writer.send_binary || !writer.send_binary(200, "image/bmp", bmp.data(), bmp.size(), writer.context))
    return device_operation_error("wallpaper_send_failed", 500);
  return device_operation_ack();
}

DeviceOperationResult handle_product_pomodoro(JsonVariantConst request, JsonObject response) {
  if (!product::product_state_snapshot(product_monotonic_seconds(), product_now_ms()).time_storage_ready)
    return device_operation_error("time_storage_unavailable", 503);
  const std::string action = request["action"] | "";
  bool changed = action == "start" ? product::product_pomodoro_start(product_monotonic_seconds()) :
      action == "pause" ? product::product_pomodoro_pause(product_monotonic_seconds()) :
      action == "resume" ? product::product_pomodoro_resume(product_monotonic_seconds()) :
      action == "finish" ? product::product_pomodoro_finish() : false;
  if (!changed) return device_operation_error("pomodoro_unchanged", 409);
  return fill_product_payload(response);
}

DeviceOperationResult handle_product_reminder(JsonVariantConst request, JsonObject response) {
  if (!product::product_state_snapshot(product_monotonic_seconds(), product_now_ms()).time_storage_ready)
    return device_operation_error("time_storage_unavailable", 503);
  const std::string action = request["action"] | "";
  if (action == "add" || action == "update") {
    if (!request["due_epoch_seconds"].is<int64_t>() || !request["title"].is<const char *>())
      return device_operation_error("invalid_reminder", 400);
    const std::string repeat_name = request["repeat"] | "once";
    if (repeat_name != "once" && repeat_name != "weekly")
      return device_operation_error("invalid_reminder_repeat", 400);
    const auto repeat = repeat_name == "weekly" ? product::ReminderRepeat::Weekly : product::ReminderRepeat::Once;
    const auto id = request["id"] | 0U;
    const bool ok = action == "add" ? product::product_add_reminder(request["due_epoch_seconds"].as<int64_t>(), repeat, request["title"].as<std::string>()) != 0
                                    : product::product_update_reminder(id, request["due_epoch_seconds"].as<int64_t>(), repeat, request["title"].as<std::string>());
    if (!ok) return device_operation_error("reminder_write_failed", 409);
  } else if (action == "remove") {
    if (!product::product_remove_reminder(request["id"] | 0U)) return device_operation_error("reminder_not_found", 404);
  } else if (action == "handle") {
    if (!product::product_handle_reminder(request["id"] | 0U)) return device_operation_error("reminder_not_found", 404);
  } else return device_operation_error("invalid_reminder_action", 400);
  return fill_product_payload(response);
}

DeviceOperationResult handle_product_computer(JsonVariantConst request, JsonObject response) {
  const std::string action = request["action"] | "";
  const uint32_t protocol_version = request["protocol_version"] | 1U;
  if (protocol_version != 1U)
    return device_operation_error("unsupported_computer_protocol", 400);
  if (action == "pair") {
    if (!product::product_pair_computer(request["computer_id"] | "")) return device_operation_error("invalid_computer_id", 400);
  } else if (action == "disconnect") {
    if (!product::product_disconnect_computer(request["computer_id"] | ""))
      return device_operation_error("computer_not_paired", 403);
  }
  else if (action == "clear") product::product_clear_computer();
  else if (action == "sample") {
    product::ComputerTelemetry sample;
    const auto parse_status = [](JsonVariantConst value) {
      const std::string status = value | "unavailable";
      if (status == "fresh") return product::TelemetryStatus::Fresh;
      if (status == "no_permission") return product::TelemetryStatus::NoPermission;
      if (status == "offline") return product::TelemetryStatus::Offline;
      if (status == "stale") return product::TelemetryStatus::Stale;
      return product::TelemetryStatus::Unavailable;
    };
    sample.cpu = parse_status(request["cpu_status"]);
    sample.gpu = parse_status(request["gpu_status"]);
    sample.temperature = parse_status(request["temperature_status"]);
    sample.fan = parse_status(request["fan_status"]);
    const auto read_metric = [&](const char *key, product::TelemetryStatus status,
                                 float minimum, float maximum, float &output) {
      if (status != product::TelemetryStatus::Fresh) return true;
      if (!request[key].is<float>()) return false;
      output = request[key].as<float>();
      return std::isfinite(output) && output >= minimum && output <= maximum;
    };
    if (!read_metric("cpu_percent", sample.cpu, 0, 100, sample.cpu_percent) ||
        !read_metric("gpu_percent", sample.gpu, 0, 100, sample.gpu_percent) ||
        !read_metric("temperature_c", sample.temperature, -100, 250, sample.temperature_c) ||
        !read_metric("fan_rpm", sample.fan, 0, 100000, sample.fan_rpm))
      return device_operation_error("invalid_telemetry_value", 400);
    if (!product::product_update_computer(request["computer_id"] | "", sample, product_now_ms()))
      return device_operation_error("computer_not_paired", 403);
  } else return device_operation_error("invalid_computer_action", 400);
  return fill_product_payload(response);
}

DeviceOperationResult handle_product_game(JsonVariantConst request, JsonObject response) {
  const std::string game = request["game"] | "";
  if (game == "wood_fish" && request["action"] == "hit") product::product_hit_wood_fish(product_now_ms());
  else if (game == "2048" && request["action"] == "reset") product::product_reset_2048();
  else if (game == "2048" && request["action"] == "move") {
    const int dx = request["dx"] | 0;
    const int dy = request["dy"] | 0;
    if (!product::product_move_2048(dx, dy)) return device_operation_error("invalid_or_blocked_move", 409);
  } else return device_operation_error("invalid_game_action", 400);
  return fill_product_payload(response);
}

DeviceOperationResult handle_weather_configure(JsonVariantConst request, JsonObject response) {
  if (!request["enabled"].is<bool>() || !request["place"].is<const char *>() ||
      !request["latitude"].is<double>() || !request["longitude"].is<double>())
    return device_operation_error("weather_config_invalid", 400);
  product::WeatherConfig config{request["enabled"].as<bool>(), request["place"].as<std::string>(),
      request["latitude"].as<double>(), request["longitude"].as<double>()};
  std::string error;
  if (!product::weather_configure(config, error)) return device_operation_error(error.c_str(), 409);
  if (config.enabled) (void)product::weather_request_refresh(error);
  return fill_product_payload(response);
}

DeviceOperationResult handle_weather_refresh(JsonVariantConst, JsonObject response) {
  std::string error;
  if (!product::weather_request_refresh(error) && error != "weather_refresh_pending")
    return device_operation_error(error.c_str(), 409);
  return fill_product_payload(response);
}
}
}
