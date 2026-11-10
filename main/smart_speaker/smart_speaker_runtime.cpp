#include "smart_speaker_lyrics_store.h"
#include "smart_speaker_runtime.h"
#include "runtime_internal.h"
#include "transfer_internal.h"
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
#include <esp_attr.h>
#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_random.h>
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
#include "smart_speaker_touch_routing.h"
#include "smart_speaker_xiaozhi.h"
#include "lunaredu_pet_conversation.h"

namespace smart_speaker {
using namespace lunaredu::lunaros;
namespace detail {

constexpr const char *kTag = "smart_speaker";

constexpr const char *kMusicDir = "/sdcard/music";

constexpr uint32_t kAudioDebugIntervalMs = 500;
constexpr uint16_t kButtonShortPressMs = 120;
constexpr uint32_t kButtonCommandCooldownMs = 500;
constexpr uint32_t kEncoderCommandCooldownMs = 45;
constexpr uint32_t kEncoderIdleClearMs = 140;
constexpr uint32_t kEncoderEdgeDebounceMs = 1;
constexpr uint32_t kFirstFrameTimeoutMs = 3000;

const DeviceCapabilityDescriptor kCapability = {
    "smart_speaker",
    nullptr,
    0,
};
Runtime g_runtime;
bool g_audio_player_created = false;

// Never block the UI task on a long SD upload.
std::mutex g_player_operation_mutex;
QueueHandle_t g_command_queue = nullptr;
std::unique_ptr<lunaredu::device::DeviceGpioButton> g_previous_button;
std::unique_ptr<lunaredu::device::DeviceGpioButton> g_next_button;
portMUX_TYPE g_encoder_lock = portMUX_INITIALIZER_UNLOCKED;
volatile int32_t g_encoder_delta = 0;
volatile uint8_t g_encoder_state = 0;
volatile TickType_t g_encoder_last_edge_tick = 0;
uint32_t g_last_encoder_command_ms = 0;
// Coalesce rotary volume input without filling the playback command queue.
std::atomic<int> g_pending_volume_delta{0};
std::array<AudioDebugFrame, kAudioDebugFrameCount> g_audio_debug_frames = {};
portMUX_TYPE g_audio_debug_lock = portMUX_INITIALIZER_UNLOCKED;
uint32_t g_audio_debug_sequence = 0;
uint32_t g_last_audio_debug_capture_ms = 0;
bool g_touch_ptt_active = false;
bool g_touch_ptt_release_pending = false;
std::atomic<bool> g_provisioning_requested{false};
std::atomic<bool> g_provisioning_cancel_requested{false};
bool g_provisioning_observed = false;  // Main input executor owns this flag.
std::mutex g_menu_mutex;
LocalMenuState g_menu;
bool g_pairing_retry = false;
struct LocalButtonState {
  bool raw = false;
  bool down = false;
  bool consumed = false;
  uint32_t changed_ms = 0;
  uint32_t pressed_ms = 0;
};
LocalButtonState g_local_left, g_local_right;
std::atomic<uint32_t> g_visual_activity{0};

bool enqueue_command(CommandType type, int value, uint32_t playlist_revision) {
  if (g_command_queue == nullptr) {
    return false;
  }
  if (type == CommandType::kAdjustVolume) {
    int pending = g_pending_volume_delta.load(std::memory_order_relaxed);
    while (!g_pending_volume_delta.compare_exchange_weak(
        pending, static_cast<int>(std::clamp<int64_t>(
                     int64_t(pending) + value, -kMaxVolume, kMaxVolume)),
        std::memory_order_relaxed)) {}
  } else {
    const Command command{type, value, playlist_revision};
    if (xQueueSend(g_command_queue, &command, 0) != pdTRUE) return false;
  }
  character::Action action = character::Action::None;
  switch (type) {
    case CommandType::kSetVolume:
    case CommandType::kAdjustVolume: action = character::Action::Volume; break;
    case CommandType::kPrevious: action = character::Action::Previous; break;
    case CommandType::kNext: action = character::Action::Next; break;
    case CommandType::kPlayTrack:
    case CommandType::kPlay: action = character::Action::Play; break;
    case CommandType::kPause: action = character::Action::Pause; break;
    case CommandType::kStop: action = character::Action::Stop; break;
    default: break;
  }
  if (action != character::Action::None) {
    uint32_t previous = g_visual_activity.load(std::memory_order_relaxed);
    uint32_t next;
    do {
      next = ((previous + 8U) & ~7U) | static_cast<uint32_t>(action);
    } while (!g_visual_activity.compare_exchange_weak(previous, next,
                                                       std::memory_order_relaxed));
  }
  return true;
}

uint32_t now_ms() {
  return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

int clamp_volume(int volume) { return std::clamp(volume, 0, kMaxVolume); }

void capture_dual_audio_debug_if_due() {
  const uint32_t current_ms = now_ms();
  if (current_ms - g_last_audio_debug_capture_ms < kAudioDebugIntervalMs) return;
  g_last_audio_debug_capture_ms = current_ms;
  const dsp::Diagnostics dsp_status = dual_i2s_dsp_diagnostics();
  const DualI2sDiagnostics i2s = dual_i2s_diagnostics();
  AudioDebugFrame frame;
  frame.sequence = ++g_audio_debug_sequence;
  frame.timestamp_ms = current_ms;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    frame.volume = g_runtime.volume;
  }
  frame.peak_in = static_cast<int>(dsp_status.input_peak * 32767.0f);
  frame.peak_out = static_cast<int>(dsp_status.output_peak * 32767.0f);
  frame.avg_abs_out = static_cast<int>(dsp_status.rms * 32767.0f);
  frame.limiter_gain_percent = static_cast<int>(dsp_status.limiter_gain * 100.0f);
  frame.limiter_events = static_cast<uint32_t>(dsp_status.limiter_events);
  frame.soft_clip_events = static_cast<uint32_t>(dsp_status.soft_clip_samples);
  frame.i2s_write_errors = i2s.timeouts_left + i2s.timeouts_right;
  frame.i2s_short_writes = i2s.short_writes_left + i2s.short_writes_right;
  frame.max_write_gap_ms = i2s.maximum_write_gap_ms;
  frame.max_write_time_ms = i2s.maximum_write_time_ms;
  constexpr uint32_t kDmaCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
  frame.dma_free_bytes = heap_caps_get_free_size(kDmaCaps);
  frame.dma_largest_block = heap_caps_get_largest_free_block(kDmaCaps);
  frame.dma_minimum_free_bytes = heap_caps_get_minimum_free_size(kDmaCaps);
  portENTER_CRITICAL(&g_audio_debug_lock);
  g_audio_debug_frames[frame.sequence % kAudioDebugFrameCount] = frame;
  portEXIT_CRITICAL(&g_audio_debug_lock);
}





bool has_mp3_signature(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == nullptr) return false;
  alignas(4) uint8_t header[4] = {};
  const size_t bytes_read = fread(header, 1, sizeof(header), file);
  fclose(file);
  if (bytes_read < 3) return false;
  if (header[0] == 'I' && header[1] == 'D' && header[2] == '3') return true;
  if (bytes_read < 4 || header[0] != 0xFF ||
      (header[1] & 0xE0) != 0xE0) {
    return false;
  }
  const uint8_t version = header[1] & 0x18;
  const uint8_t layer = header[1] & 0x06;
  const uint8_t bitrate = header[2] & 0xF0;
  const uint8_t sample_rate = header[2] & 0x0C;
  return version != 0x08 && layer != 0x00 && bitrate != 0x00 &&
         bitrate != 0xF0 && sample_rate != 0x0C;
}

void set_error_locked(const char *error) {
  g_runtime.last_error = error == nullptr ? "" : error;
  if (!g_runtime.last_error.empty()) {
    ESP_LOGW(kTag, "%s", g_runtime.last_error.c_str());
  }
}

void clear_error_locked() { g_runtime.last_error.clear(); }

bool ensure_music_dir() {
  if (mkdir(kMusicDir, 0775) == 0 || errno == EEXIST) {
    return true;
  }
  ESP_LOGW(kTag, "mkdir failed path=%s errno=%d", kMusicDir, errno);
  return false;
}

void append_directory_tracks(const char *dir_path,
                             std::vector<TrackInfo> &tracks) {
  DIR *dir = opendir(dir_path);
  if (dir == nullptr) {
    return;
  }

  while (dirent *entry = readdir(dir)) {
    if (entry->d_name[0] == '.') {
      continue;
    }
    const std::string name = entry->d_name;
    if (!ends_with_mp3(name)) {
      continue;
    }
    std::string path = std::string(dir_path) + "/" + name;
    struct stat info = {};
    if (stat(path.c_str(), &info) != 0 || S_ISDIR(info.st_mode)) {
      continue;
    }
    if (!has_mp3_signature(path.c_str())) {
      ESP_LOGW(kTag, "ignoring invalid MP3 entry path=%s size=%llu",
               path.c_str(), static_cast<unsigned long long>(info.st_size));
      continue;
    }
    TrackInfo track;
    track.name = name;
    track.path = std::move(path);
    track.has_lyrics = lyrics::exists(lyrics::readable(lyrics::path_for(track.path)));
    track.lyrics_delay_ms = lyrics::delay(track.path);
    track.size_bytes = static_cast<size_t>(std::max<off_t>(0, info.st_size));
    tracks.push_back(std::move(track));
  }
  closedir(dir);
}

std::vector<TrackInfo> scan_tracks() {
  std::vector<TrackInfo> tracks;
  append_directory_tracks("/sdcard", tracks);
  append_directory_tracks(kMusicDir, tracks);

  std::sort(tracks.begin(), tracks.end(),
            [](const TrackInfo &a, const TrackInfo &b) {
              return lower_copy(a.name) < lower_copy(b.name);
            });
  tracks.erase(std::unique(tracks.begin(), tracks.end(),
                           [](const TrackInfo &a, const TrackInfo &b) {
                             return a.path == b.path;
                           }),
               tracks.end());
  return tracks;
}

void rescan_locked() {
  ++g_runtime.playlist_revision;
  if (!lunaredu::device::tfcard_store_ready()) {
    g_runtime.sd_ready = false;
    g_runtime.playlist.clear();
    g_runtime.current_index = -1;
    set_error_locked("sdcard_not_ready");
    return;
  }

  g_runtime.sd_ready = true;
  (void)ensure_music_dir();
  const std::string current_path =
      g_runtime.current_index >= 0 &&
              g_runtime.current_index <
                  static_cast<int>(g_runtime.playlist.size())
          ? g_runtime.playlist[static_cast<size_t>(g_runtime.current_index)].path
          : std::string();
  g_runtime.playlist = scan_tracks();
  g_runtime.current_index = -1;
  if (!current_path.empty()) {
    for (size_t i = 0; i < g_runtime.playlist.size(); ++i) {
      if (g_runtime.playlist[i].path == current_path) {
        g_runtime.current_index = static_cast<int>(i);
        break;
      }
    }
  }
  if (g_runtime.current_index < 0 && !g_runtime.playlist.empty()) {
    g_runtime.current_index = 0;
  }
  if (g_runtime.current_index < 0 ||
      g_runtime.playlist[size_t(g_runtime.current_index)].path != current_path) g_runtime.lyrics.reset();
  clear_error_locked();
  ESP_LOGI(kTag, "playlist scanned tracks=%u",
           static_cast<unsigned>(g_runtime.playlist.size()));
}

esp_err_t i2s_write_scaled(void *audio_buffer, size_t len,
                           size_t *bytes_written, uint32_t timeout_ms,
                           void *ctx) {
  const esp_err_t err = dual_i2s_audio_write(audio_buffer, len, bytes_written,
                                              timeout_ms, ctx);
  if (err == ESP_OK && bytes_written != nullptr && *bytes_written > 0) {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.first_frame_seen = true;
    g_runtime.decode_timeout_skip_sent = false;
  }
  return err;
}

esp_err_t i2s_reconfig_clock(uint32_t rate, uint32_t bits_cfg,
                             i2s_slot_mode_t channels) {
  return dual_i2s_audio_reconfigure(rate, bits_cfg, channels);
}

esp_err_t audio_mute(AUDIO_PLAYER_MUTE_SETTING setting) {
  // UNMUTE is dispatched by the decoder exactly once at each file boundary,
  // including consecutive tracks of the same sample rate. Resume does not reset.
  if (setting == AUDIO_PLAYER_UNMUTE) dual_i2s_track_clock_reset();
  ESP_LOGI(kTag, "audio mute=%d", static_cast<int>(setting));
  return dual_i2s_audio_mute(setting);
}

bool setup_audio() {
  if (!setup_dual_i2s_audio()) return false;

  audio_player_config_t player_config = {};
  player_config.mute_fn = audio_mute;
  player_config.clk_set_fn = i2s_reconfig_clock;
  player_config.write_fn2 = i2s_write_scaled;
  player_config.write_ctx = &g_runtime;
  player_config.priority = 8;
  // Split decoding and PCM output across cores so background TLS and controls
  // retain CPU headroom, including during 48 kHz playback.
  player_config.coreID = 1;
  player_config.force_stereo = true;

  const esp_err_t err = audio_player_new(player_config);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "audio_player_new failed: %s", esp_err_to_name(err));
    (void)stop_dual_i2s_audio();
    return false;
  }
  g_audio_player_created = true;
  return true;
}

void update_play_state(bool playing, bool paused, bool auto_advance) {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  g_runtime.playing = playing;
  g_runtime.paused = paused;
  if (!paused) g_runtime.paused_by_volume = false;
  g_runtime.auto_advance = auto_advance;
}

bool play_index(int index) {
  std::string path;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!g_runtime.audio_ready) {
      set_error_locked("audio_not_ready");
      return false;
    }
    if (g_runtime.playlist.empty()) {
      set_error_locked("playlist_empty");
      return false;
    }
    if (index < 0) index = 0;
    index %= static_cast<int>(g_runtime.playlist.size());
    path = g_runtime.playlist[static_cast<size_t>(index)].path;
  }
  FILE *file = fopen(path.c_str(), "rb");
  if (file == nullptr) {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    set_error_locked("track_open_failed");
    return false;
  }
  const auto track_lyrics = lyrics::load(path);
  const int lyric_delay = lyrics::delay(path);
  (void)dual_i2s_audio_fade_to_mute(100);
  dual_i2s_track_clock_reset();
  {
    // Publish the new playback generation before queueing it. The audio task
    // may decode and deliver its first PCM block immediately after
    // audio_player_play(), so resetting first_frame_seen afterwards loses that
    // event and can cause a false first-frame timeout/skip.
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.lyrics = track_lyrics;
    g_runtime.lyrics_delay_ms = lyric_delay;
    g_runtime.current_index = index;
    g_runtime.paused_by_volume = false;
    g_runtime.playing = true;
    g_runtime.paused = false;
    g_runtime.auto_advance = true;
    g_runtime.first_frame_seen = false;
    g_runtime.decode_timeout_skip_sent = false;
    g_runtime.play_started_ms = now_ms();
    ++g_runtime.playback_generation;
    g_runtime.advance_queued = false;
    clear_error_locked();
  }
  const esp_err_t err = audio_player_play(file);
  if (err != ESP_OK) {
    fclose(file);
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.playing = false;
    g_runtime.paused = false;
    set_error_locked("track_play_failed");
    return false;
  }
  dual_i2s_audio_fade_up();
  ESP_LOGI(kTag, "playing index=%d path=%s", index, path.c_str());
  return true;
}

void queue_track_completion(bool failed) {
  uint32_t generation = 0;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!g_runtime.auto_advance || !g_runtime.playing || g_runtime.uploading ||
        g_runtime.advance_queued || g_runtime.playlist.empty()) return;
    failed = failed || !g_runtime.first_frame_seen;
    g_runtime.playing = false;
    g_runtime.paused = false;
    g_runtime.advance_queued = true;
    g_runtime.paused_by_volume = false;
    generation = g_runtime.playback_generation;
  }
  if (!enqueue_command(failed ? CommandType::kSkipFailed : CommandType::kAutoNext, 0, generation)) {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.advance_queued = false;
    set_error_locked("track_advance_queue_full");
  }
}

void cycle_playback_mode() {
  PlaybackMode mode;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    mode = next_playback_mode(g_runtime.playback_mode);
  }
  nvs_handle_t handle = 0;
  esp_err_t result = nvs_open("spkr_player", NVS_READWRITE, &handle);
  if (result == ESP_OK) {
    result = nvs_set_u8(handle, "mode", static_cast<uint8_t>(mode));
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
  }
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if (result != ESP_OK) { set_error_locked("playback_mode_save_failed"); return; }
  g_runtime.playback_mode = mode;
  ++g_runtime.mode_sequence;
  g_runtime.mode_updated_ms = now_ms();
  uint32_t previous = g_visual_activity.load(std::memory_order_relaxed);
  uint32_t next;
  do { next = ((previous + 8U) & ~7U) | static_cast<uint32_t>(character::Action::Mode); }
  while (!g_visual_activity.compare_exchange_weak(previous, next, std::memory_order_relaxed));
  ESP_LOGI(kTag, "playback mode=%s", playback_mode_name(mode));
}

void handle_audio_event(audio_player_cb_ctx_t *ctx) {
  if (ctx == nullptr) {
    return;
  }
  switch (ctx->audio_event) {
  case AUDIO_PLAYER_CALLBACK_EVENT_PLAYING:
  case AUDIO_PLAYER_CALLBACK_EVENT_COMPLETED_PLAYING_NEXT:
    update_play_state(true, false, true);
    break;
  case AUDIO_PLAYER_CALLBACK_EVENT_PAUSE:
    update_play_state(false, true, true);
    break;
  case AUDIO_PLAYER_CALLBACK_EVENT_IDLE: {
    queue_track_completion(false);
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.playing = false;
    g_runtime.paused = false;
    g_runtime.paused_by_volume = false;
    break;
  }
  case AUDIO_PLAYER_CALLBACK_EVENT_UNKNOWN_FILE_TYPE:
    queue_track_completion(true);
    break;
  default:
    break;
  }
}

void process_command(const Command &command) {
  if (command.type == CommandType::kCycleMode) { cycle_playback_mode(); return; }
  std::unique_lock<std::mutex> player_lock(g_player_operation_mutex, std::try_to_lock);
  if (!player_lock.owns_lock()) return;
  // Explicit transport commands override a pause caused by muting, including
  // a second manual pause while already muted. Volume alone may resume only
  // the playback it paused; it must never start an idle or stopped player.
  if (command.type == CommandType::kPlay || command.type == CommandType::kPlayTrack ||
      command.type == CommandType::kPause || command.type == CommandType::kStop ||
      command.type == CommandType::kNext || command.type == CommandType::kPrevious ||
      command.type == CommandType::kRescan) {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.paused_by_volume = false;
  }
  switch (command.type) {
  case CommandType::kPlayTrack: {
    int index = -1;
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      // A rescan between admission and execution must never select a different song.
      g_runtime.failed_tracks = 0;
      if (command.playlist_revision == g_runtime.playlist_revision &&
          command.value >= 0 && static_cast<size_t>(command.value) < g_runtime.playlist.size())
        index = command.value;
      if (index < 0) set_error_locked("track_not_found");
    }
    if (index >= 0) (void)play_index(index);
    break;
  }
  case CommandType::kPlay: {
    bool paused = false;
    int current_index = -1;
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      paused = g_runtime.paused;
      g_runtime.failed_tracks = 0;
      current_index = g_runtime.current_index;
    }
    if (paused) {
      if (audio_player_resume() == ESP_OK) {
        update_play_state(true, false, true);
        dual_i2s_audio_fade_up();
      }
      return;
    }
    (void)play_index(current_index);
    break;
  }
  case CommandType::kPause:
    (void)dual_i2s_audio_fade_to_mute(100);
    if (audio_player_pause() == ESP_OK) update_play_state(false, true, true);
    break;
  case CommandType::kStop:
    (void)dual_i2s_audio_fade_to_mute(100);
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      g_runtime.auto_advance = false;
    }
    (void)audio_player_stop();
    update_play_state(false, false, false);
    break;
  case CommandType::kNext:
  case CommandType::kAutoNext:
  case CommandType::kSkipFailed: {
    int next_index = -1;
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      const bool automatic = command.type != CommandType::kNext;
      if (automatic && (!g_runtime.auto_advance || !g_runtime.advance_queued || g_runtime.uploading || g_runtime.paused ||
                        command.playlist_revision != g_runtime.playback_generation)) break;
      g_runtime.advance_queued = false;
      const bool failed = command.type == CommandType::kSkipFailed;
      g_runtime.failed_tracks = failed ? g_runtime.failed_tracks + 1 : 0;
      if (failed && g_runtime.failed_tracks >= g_runtime.playlist.size()) {
        g_runtime.auto_advance = false;
        set_error_locked("playlist_unplayable");
        break;
      }
      next_index = advance_track(g_runtime.playback_mode, failed ? TrackAdvance::Failed :
          automatic ? TrackAdvance::Finished : TrackAdvance::Next, g_runtime.current_index,
          static_cast<int>(g_runtime.playlist.size()), esp_random());
      if (automatic && next_index < 0) g_runtime.auto_advance = false;
    }
    if (next_index >= 0) (void)play_index(next_index);
    break;
  }
  case CommandType::kPrevious: {
    int previous_index = -1;
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      g_runtime.failed_tracks = 0;
      if (!g_runtime.playlist.empty()) {
        const int count = static_cast<int>(g_runtime.playlist.size());
        const int current =
            g_runtime.current_index < 0 ? 0 : g_runtime.current_index;
        previous_index = (current - 1 + count) % count;
      }
    }
    if (previous_index >= 0) (void)play_index(previous_index);
    break;
  }
  case CommandType::kSetVolume:
  case CommandType::kAdjustVolume: {
    int volume = 0;
    bool pause_at_zero = false;
    bool resume_from_zero = false;
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      const int previous = g_runtime.volume;
      volume = clamp_volume(command.type == CommandType::kSetVolume ? command.value : previous + command.value);
      g_runtime.volume = volume;
      pause_at_zero = volume == 0 && previous > 0 && g_runtime.playing;
      resume_from_zero = volume > 0 && g_runtime.paused && g_runtime.paused_by_volume;
    }
    dual_i2s_audio_set_volume(volume);
    if (pause_at_zero) {
      (void)dual_i2s_audio_fade_to_mute(100);
      if (audio_player_pause() == ESP_OK) {
        update_play_state(false, true, true);
        std::lock_guard<std::mutex> lock(g_runtime.mutex);
        g_runtime.paused_by_volume = true;
      }
      else { std::lock_guard<std::mutex> lock(g_runtime.mutex); set_error_locked("playback_pause_failed"); }
    } else if (resume_from_zero) {
      if (audio_player_resume() == ESP_OK) {
        update_play_state(true, false, true);
        dual_i2s_audio_fade_up();
      } else {
        std::lock_guard<std::mutex> lock(g_runtime.mutex);
        set_error_locked("playback_resume_failed");
      }
    }
    ESP_LOGI(kTag, "volume=%d max=%d", volume, kMaxVolume);
    break;
  }
  case CommandType::kRescan:
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      rescan_locked();
    }
    break;
  case CommandType::kCycleMode: break; // Handled before the player lock.
  }
}

void IRAM_ATTR encoder_isr(void *arg) {
  (void)arg;
  const uint8_t a =
      gpio_get_level(static_cast<gpio_num_t>(
          CONFIG_LUNAREDU_SMART_SPEAKER_ENCODER_A_GPIO)) != 0
          ? 1
          : 0;
  const uint8_t b =
      gpio_get_level(static_cast<gpio_num_t>(
          CONFIG_LUNAREDU_SMART_SPEAKER_ENCODER_B_GPIO)) != 0
          ? 1
          : 0;
  const uint8_t state = static_cast<uint8_t>((a << 1) | b);
  // GPIO interrupts remain enabled during flash/NVS writes. Keep every
  // lookup used by this IRAM handler in internal RAM while cache is disabled.
  static const DRAM_ATTR int8_t kTransition[16] = {
      0, -1, 1, 0,
      1, 0, 0, -1,
      -1, 0, 0, 1,
      0, 1, -1, 0,
  };
  const TickType_t now_tick = xTaskGetTickCountFromISR();
  portENTER_CRITICAL_ISR(&g_encoder_lock);
  if (g_encoder_last_edge_tick != 0 &&
      now_tick - g_encoder_last_edge_tick < pdMS_TO_TICKS(kEncoderEdgeDebounceMs)) {
    g_encoder_state = state;
    portEXIT_CRITICAL_ISR(&g_encoder_lock);
    return;
  }
  const uint8_t index = static_cast<uint8_t>((g_encoder_state << 2) | state);
  const int8_t step = kTransition[index & 0x0F];
  g_encoder_delta += step;
  if (step != 0) {
    g_encoder_last_edge_tick = now_tick;
  }
  g_encoder_state = state;
  portEXIT_CRITICAL_ISR(&g_encoder_lock);
}

void setup_encoder() {
  gpio_config_t config = {};
  config.intr_type = GPIO_INTR_ANYEDGE;
  config.mode = GPIO_MODE_INPUT;
  config.pin_bit_mask =
      (1ULL << CONFIG_LUNAREDU_SMART_SPEAKER_ENCODER_A_GPIO) |
      (1ULL << CONFIG_LUNAREDU_SMART_SPEAKER_ENCODER_B_GPIO);
  config.pull_up_en = GPIO_PULLUP_ENABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&config));

  g_encoder_state =
      static_cast<uint8_t>(
          (gpio_get_level(static_cast<gpio_num_t>(
               CONFIG_LUNAREDU_SMART_SPEAKER_ENCODER_A_GPIO))
           << 1) |
          gpio_get_level(static_cast<gpio_num_t>(
              CONFIG_LUNAREDU_SMART_SPEAKER_ENCODER_B_GPIO)));
  esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(kTag, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
    return;
  }
  ESP_ERROR_CHECK(gpio_isr_handler_add(
      static_cast<gpio_num_t>(CONFIG_LUNAREDU_SMART_SPEAKER_ENCODER_A_GPIO),
      encoder_isr, nullptr));
  ESP_ERROR_CHECK(gpio_isr_handler_add(
      static_cast<gpio_num_t>(CONFIG_LUNAREDU_SMART_SPEAKER_ENCODER_B_GPIO),
      encoder_isr, nullptr));
}

// Both menu and runtime mutexes are held. Keep selection tied to a path when
// uploads/removals change indexes while the user is browsing the library.
void sync_local_library_locked() {
  if (g_menu.library_revision != g_runtime.playlist_revision) {
    auto found = std::find_if(g_runtime.playlist.begin(), g_runtime.playlist.end(),
        [](const TrackInfo &track) { return track.path == g_menu.library_path; });
    if (found != g_runtime.playlist.end())
      g_menu.library_index = static_cast<int>(found - g_runtime.playlist.begin());
  }
  g_menu.library_index = g_runtime.playlist.empty() ? 0 :
      std::clamp(g_menu.library_index, 0, static_cast<int>(g_runtime.playlist.size()) - 1);
  g_menu.library_revision = g_runtime.playlist_revision;
  g_menu.library_path = g_runtime.playlist.empty() ? "" : g_runtime.playlist[g_menu.library_index].path;
}

// The right button and a touch tap both mean "confirm" while a local menu is
// open. Keep the state transitions in one place so a touch tap never falls
// through to the home-screen play/pause action.
bool route_local_menu_button_locked(bool right,
                                    NetworkProvisioningState provisioning,
                                    NetworkLinkState link) {
  if (g_menu.provisioning || (!g_menu.provisioning_dismissed &&
                              provisioning != NetworkProvisioningState::Idle)) {
    if (!right) {
      g_provisioning_requested.store(false);
      g_provisioning_cancel_requested.store(true);
      g_menu.provisioning = false;
      g_menu.provisioning_dismissed = true;
      g_provisioning_observed = false;
      g_menu.open = true;
      g_menu.binding = false;
      g_menu.information = false;
      g_menu.network = false;
      g_menu.library = false;
      g_menu.selected = 0;
      g_menu.hold_percent = 0;
      g_local_right.consumed = true;
    }
    return true;
  }
  if (g_menu.network) {
    if (!right) {
      g_menu.network = false;
      g_menu.open = true;
    } else if (g_menu.network_selected == 1) {
      g_menu.network = false;
      g_menu.provisioning = true;
      g_menu.provisioning_dismissed = false;
      g_provisioning_observed = false;
      g_provisioning_requested.store(true);
    }
    return true;
  }
  if (g_menu.library) {
    if (!right) {
      g_menu.library = false;
      g_menu.open = true;
    } else {
      std::lock_guard<std::mutex> runtime_lock(g_runtime.mutex);
      // Never play a replacement that moved into a deleted selection.
      const std::string selected_path = g_menu.library_path;
      sync_local_library_locked();
      if (selected_path != g_menu.library_path) g_menu.library_notice = "歌曲库已更新";
      else if (g_runtime.uploading) g_menu.library_notice = "正在接收歌曲";
      else if (!g_runtime.audio_ready) g_menu.library_notice = "播放暂不可用";
      else if (!g_runtime.playlist.empty() &&
               enqueue_command(CommandType::kPlayTrack, g_menu.library_index,
                               g_menu.library_revision))
        g_menu.library = false;
    }
    return true;
  }
  if (g_menu.information) {
    if (!right) {
      g_menu.information = false;
      g_menu.open = true;
    } else {
      g_menu.information_page = (g_menu.information_page + 1) % 3;
    }
    return true;
  }
  if (g_menu.binding) {
    if (!right) {
      if (g_menu.unbind_confirm) {
        g_menu.unbind_confirm = false;
        g_menu.account_selected = 1;
      } else {
        g_menu.binding = false;
        g_menu.open = true;
      }
    } else if (g_menu.unbind_pending) {
      // Single request, even after re-entry.
    } else if (g_menu.unbind_confirm) {
      g_menu.unbind_confirm = false;
      if (g_menu.account_selected == 1) {
        g_menu.unbind_pending = request_device_link_unbind();
        g_menu.account_notice = g_menu.unbind_pending ? "" :
            link != NetworkLinkState::Up ? "请先联网" : "账号状态已变化";
      }
      g_menu.account_selected = 1;
    } else {
      const auto pairing = device_link_pairing_snapshot();
      if (pairing.state == DeviceLinkBindingState::Bound) {
        if (g_menu.account_selected == 1) {
          g_menu.unbind_confirm = true;
          g_menu.account_selected = 0;
          g_menu.account_notice.clear();
        }
        return true;
      }
      g_menu.account_notice.clear();
      const bool expired = pairing.state == DeviceLinkBindingState::Pairing &&
          pairing.expires_at_ms <= static_cast<int64_t>(std::time(nullptr)) * 1000;
      if (expired) g_pairing_retry = cancel_device_link_pairing();
      else if (pairing.state != DeviceLinkBindingState::Bound &&
               pairing.state != DeviceLinkBindingState::Pairing &&
               pairing.state != DeviceLinkBindingState::Connecting)
        (void)request_device_link_pairing();
    }
    return true;
  }
  if (g_menu.open) {
    if (!right) {
      g_menu.open = false;
    } else if (g_menu.selected == 2) {
      g_menu.open = false;
      g_menu.information = true;
      g_menu.information_page = 0;
    } else if (g_menu.selected == 1) {
      g_menu.open = false;
      g_menu.binding = true;
      g_menu.account_selected = 0;
      g_menu.unbind_confirm = false;
      if (!g_menu.unbind_pending && g_menu.account_notice.empty() &&
          device_link_pairing_snapshot().state != DeviceLinkBindingState::Bound)
        (void)request_device_link_pairing();
    } else if (g_menu.selected == 0) {
      g_menu.network = true;
      g_menu.network_selected = 0;
      g_menu.open = false;
    } else if (g_menu.selected == 3) {
      g_menu.library = true;
      g_menu.open = false;
      g_menu.library_notice.clear();
      std::lock_guard<std::mutex> runtime_lock(g_runtime.mutex);
      g_menu.library_index = std::max(0, g_runtime.current_index);
      g_menu.library_revision = g_runtime.playlist_revision;
      sync_local_library_locked();
    }
    return true;
  }
  return false;
}

bool route_local_encoder(int delta) {
  std::lock_guard<std::mutex> lock(g_menu_mutex);
  if (g_menu.binding) {
    if (!g_menu.unbind_pending && (g_menu.unbind_confirm ||
        device_link_pairing_snapshot().state == DeviceLinkBindingState::Bound)) {
      g_menu.account_selected = 1 - g_menu.account_selected;
      g_menu.account_notice.clear();
    }
    return true;
  }
  if (g_menu.provisioning ||
      network_provisioning_state() != NetworkProvisioningState::Idle) return true;
  if (g_menu.open) {
    g_menu.selected = (g_menu.selected + (delta < 0 ? 1 : 3)) % 4;
    return true;
  }
  if (g_menu.information) {
    g_menu.information_page = (g_menu.information_page + (delta < 0 ? 1 : 2)) % 3;
    return true;
  }
  if (g_menu.network) {
    g_menu.network_selected = 1 - g_menu.network_selected;
    return true;
  }
  if (g_menu.library) {
    std::lock_guard<std::mutex> runtime_lock(g_runtime.mutex);
    sync_local_library_locked();
    const int count = static_cast<int>(g_runtime.playlist.size());
    if (count) {
      g_menu.library_index = (g_menu.library_index + (delta < 0 ? 1 : count - 1)) % count;
      g_menu.library_path = g_runtime.playlist[g_menu.library_index].path;
    }
    g_menu.library_notice.clear();
    return true;
  }
  return false;
}

void poll_encoder() {
  int32_t delta = 0;
  const uint32_t current_ms = now_ms();
  const TickType_t current_tick = xTaskGetTickCount();
  portENTER_CRITICAL(&g_encoder_lock);
  int32_t raw_delta = g_encoder_delta;
  const int32_t raw_magnitude =
      raw_delta < 0 ? -raw_delta : raw_delta;
  if (raw_magnitude > 0 && raw_magnitude < 4 &&
      current_tick - g_encoder_last_edge_tick >=
          pdMS_TO_TICKS(kEncoderIdleClearMs)) {
    g_encoder_delta = 0;
    raw_delta = 0;
  }
  if ((raw_delta >= 4 || raw_delta <= -4) &&
      current_ms - g_last_encoder_command_ms >=
          kEncoderCommandCooldownMs) {
    delta = raw_delta > 0 ? 1 : -1;
    g_encoder_delta -= delta * 4;
    g_last_encoder_command_ms = current_ms;
  }
  portEXIT_CRITICAL(&g_encoder_lock);
  if (delta != 0) {
#if CONFIG_LUNAREDU_SMART_SPEAKER_ENCODER_REVERSED
    delta = -delta;
#endif
    if (route_local_encoder(delta)) return;
    adjust_volume(static_cast<int>(delta) *
                  CONFIG_LUNAREDU_SMART_SPEAKER_VOLUME_STEP);
  }
}

void setup_buttons() {
  lunaredu::device::DeviceGpioButtonConfig previous;
  previous.label = "previous";
  previous.gpio = CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_BUTTON_GPIO;
  previous.active_high = !CONFIG_LUNAREDU_SMART_SPEAKER_BUTTON_ACTIVE_LOW;
  previous.short_press_time_ms = kButtonShortPressMs;
  previous.long_press_time_ms = 2000;
  g_previous_button =
      std::make_unique<lunaredu::device::DeviceGpioButton>(previous);
  if (g_previous_button->Init() == ESP_OK) {
    // Main executor owns debounced local input and menu state.
  }

  lunaredu::device::DeviceGpioButtonConfig next_config;
  next_config.label = "next";
  next_config.gpio = CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_BUTTON_GPIO;
  next_config.active_high = !CONFIG_LUNAREDU_SMART_SPEAKER_BUTTON_ACTIVE_LOW;
  next_config.short_press_time_ms = kButtonShortPressMs;
  next_config.long_press_time_ms = 2000;
  g_next_button =
      std::make_unique<lunaredu::device::DeviceGpioButton>(next_config);
  if (g_next_button->Init() == ESP_OK) {
    // Release after a long press is consumed by poll_local_buttons().
  }
}

void poll_local_buttons() {
  const uint32_t now = now_ms();
  const auto provisioning = network_provisioning_state();
  const auto link = network_link_state();
  {
    std::lock_guard<std::mutex> lock(g_menu_mutex);
    if (g_menu.unbind_pending) {
      const auto state = device_link_unbind_state();
      if (state != DeviceLinkUnbindState::Pending) {
        g_menu.unbind_pending = false;
        g_menu.unbind_confirm = false;
        g_menu.account_selected = 0;
        g_menu.account_notice = state == DeviceLinkUnbindState::Succeeded ? "已退出账号" : "退出未完成";
      }
    }
    if (provisioning != NetworkProvisioningState::Idle && !g_menu.provisioning_dismissed)
      g_provisioning_observed = true;
    else if (provisioning == NetworkProvisioningState::Idle) {
      g_menu.provisioning_dismissed = false;
      if (g_provisioning_observed && link == NetworkLinkState::Up) {
        g_menu.provisioning = false;
        g_provisioning_observed = false;
      }
    }
  }
  const int active = CONFIG_LUNAREDU_SMART_SPEAKER_BUTTON_ACTIVE_LOW ? 0 : 1;
  auto poll = [&](LocalButtonState &button, int pin, bool right) {
    const bool raw = gpio_get_level(static_cast<gpio_num_t>(pin)) == active;
    if (raw != button.raw) { button.raw = raw; button.changed_ms = now; }
    std::lock_guard<std::mutex> lock(g_menu_mutex);
    if (raw != button.down && now - button.changed_ms >= 30) {
      button.down = raw;
      if (raw) {
        button.pressed_ms = now;
        button.consumed = false;
      } else {
        if (right) g_menu.hold_percent = 0;
        else g_menu.mode_hold_percent = 0;
        if (!button.consumed) {
          if (route_local_menu_button_locked(right, provisioning, link)) {
            // Menu action handled above.
          } else if (!right && dismiss_transfer()) {
            // Return home while the transfer continues.
          } else enqueue_command(right ? CommandType::kNext : CommandType::kPrevious);
        }
      }
    }
    if (raw && button.down && !button.consumed &&
        !g_menu.open && !g_menu.binding && !g_menu.information && !g_menu.network && !g_menu.library && !g_menu.provisioning &&
        (g_menu.provisioning_dismissed || provisioning == NetworkProvisioningState::Idle)) {
      const uint32_t held = now - button.pressed_ms;
      const bool other_down = gpio_get_level(static_cast<gpio_num_t>(right ?
          CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_BUTTON_GPIO : CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_BUTTON_GPIO)) == active;
      if (other_down) {
        g_menu.hold_percent = g_menu.mode_hold_percent = 0;
        button.consumed = true;
        (right ? g_local_left : g_local_right).consumed = true;
        return;
      }
      if (!right) {
        const auto transfer = transfer_snapshot();
        if (transfer.receiving || transfer.batch_open) return;
        g_menu.mode_hold_percent = held <= 400 ? 0 :
            static_cast<int>(std::min<uint32_t>(100, (held - 400) * 100 / 1100));
        if (held >= 1500) {
          button.consumed = true;
          g_menu.mode_hold_percent = 0;
          (void)enqueue_command(CommandType::kCycleMode);
        }
        return;
      }
      // Short track presses must never start the settings morph. Ignore the
      // release debounce interval too, so a near-threshold release cannot flash.
      g_menu.hold_percent = held <= 400 ? 0 :
          static_cast<int>(std::min<uint32_t>(100, (held - 400) * 100 / 1100));
      if (held >= 1500) {
        button.consumed = true;
        g_local_left.consumed = true;
        g_menu.open = true;
        g_menu.binding = false;
        g_menu.selected = 0;
        g_menu.hold_percent = 0;
        ESP_LOGI(kTag, "local settings opened by right-button hold");
      }
    }
  };
  poll(g_local_left, CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_BUTTON_GPIO, false);
  poll(g_local_right, CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_BUTTON_GPIO, true);
  if (g_pairing_retry) {
    const auto pairing = device_link_pairing_snapshot();
    if (pairing.state == DeviceLinkBindingState::Bound) g_pairing_retry = false;
    else if (pairing.state != DeviceLinkBindingState::Pairing && pairing.state != DeviceLinkBindingState::Connecting) {
      g_pairing_retry = false;
      (void)request_device_link_pairing();
    }
  }
}

void process_touch_gestures() {
  const TouchGesture gesture = smart_speaker_take_touch_gesture();
  if (gesture == TouchGesture::None) return;
  ESP_LOGI(kTag, "touch gesture=%s", touch_gesture_name(gesture));
  const auto menu = local_menu_snapshot();
  const bool menu_active = menu.open || menu.binding || menu.information ||
      menu.network || menu.library || menu.provisioning;
  switch (route_touch_gesture(gesture, menu_active)) {
    case TouchRoute::MenuConfirm: {
      const auto provisioning = network_provisioning_state();
      const auto link = network_link_state();
      {
        std::lock_guard<std::mutex> lock(g_menu_mutex);
        (void)route_local_menu_button_locked(true, provisioning, link);
      }
      break;
    }
    case TouchRoute::StartVoice:
      {
        const auto assistant = lunaredu::pet::conversation::snapshot();
        if (assistant.activation_ready) {
          g_touch_ptt_active = xiaozhi_start_listening();
          g_touch_ptt_release_pending = false;
        } else {
          (void)xiaozhi_start_activation();
        }
      }
      break;
    case TouchRoute::FinishVoice:
      if (g_touch_ptt_active) {
        const auto assistant = lunaredu::pet::conversation::runtime_snapshot();
        if (assistant.listening) {
          (void)xiaozhi_finish_listening();
          g_touch_ptt_active = false;
          g_touch_ptt_release_pending = false;
        } else {
          g_touch_ptt_release_pending = true;
        }
      }
      break;
    case TouchRoute::MenuBack:
      {
        const auto provisioning = network_provisioning_state();
        const auto link = network_link_state();
        {
          std::lock_guard<std::mutex> lock(g_menu_mutex);
          // Full-screen horizontal gestures are back navigation while a
          // settings page owns the screen. On the home page they retain the
          // existing previous/next-track behavior.
          if (route_local_menu_button_locked(false, provisioning, link)) break;
        }
      }
      break;
    case TouchRoute::NextTrack:
      next();
      break;
    case TouchRoute::PreviousTrack:
      previous();
      break;
    case TouchRoute::OpenSettings:
      {
        std::lock_guard<std::mutex> lock(g_menu_mutex);
        g_menu.open = true;
        g_menu.binding = false;
        g_menu.information = false;
        g_menu.network = false;
        g_menu.library = false;
        g_menu.selected = 0;
      }
      break;
    case TouchRoute::TogglePlayback: {
      const auto status = status_snapshot();
      if (status.playing && !status.paused) pause();
      else play();
      break;
    }
    case TouchRoute::None:
    default:
      break;
  }
}

void ota_lifecycle(OtaServiceLifecycleEvent event, void *) {
  if (event == OtaServiceLifecycleEvent::UploadStarting ||
      event == OtaServiceLifecycleEvent::UploadAborted ||
      event == OtaServiceLifecycleEvent::RestartRequested) {
    // Lifecycle callbacks must not block; the output task advances the ramp.
    (void)dual_i2s_audio_mute(AUDIO_PLAYER_MUTE);
  }
}

// These handlers share the player-operation lock with uploads, removal and
// track changes. Disk access never holds the UI snapshot mutex.

} // namespace detail
using namespace detail;

bool stop_audio_service(std::string &error) {
  std::lock_guard<std::mutex> operation(g_player_operation_mutex);
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.audio_ready = false;
    g_runtime.playing = g_runtime.paused = false;
  }
  if (g_audio_player_created) {
    if (audio_player_delete() != ESP_OK) { error = "decoder_stop_failed"; return false; }
    g_audio_player_created = false;
  }
  if (!stop_dual_i2s_audio()) { error = "audio_release_pending"; return false; }
  error.clear();
  return true;
}

bool start_audio_service(std::string &error) {
  if (status_snapshot().audio_ready) return true;
  if (!stop_audio_service(error)) return false;
  if (!setup_audio() || audio_player_callback_register(handle_audio_event, nullptr) != ESP_OK) {
    std::string cleanup;
    (void)stop_audio_service(cleanup);
    error = cleanup.empty() ? "audio_init_failed" : cleanup;
    return false;
  }
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  g_runtime.audio_ready = true;
  error.clear();
  return true;
}

bool setup_runtime() {
  if (!status_snapshot().audio_ready) return false;
  if (g_command_queue) return true;
  nvs_handle_t player_settings = 0;
  if (nvs_open("spkr_player", NVS_READONLY, &player_settings) == ESP_OK) {
    uint8_t mode = 0;
    if (nvs_get_u8(player_settings, "mode", &mode) == ESP_OK && mode <= 2)
      g_runtime.playback_mode = static_cast<PlaybackMode>(mode);
    nvs_close(player_settings);
  }
  g_command_queue = xQueueCreate(12, sizeof(Command));
  if (!g_command_queue) {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    set_error_locked("command_queue_allocation_failed");
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.volume = clamp_volume(kInitialVolume);
  }
  dual_i2s_audio_set_volume(kInitialVolume);

  if (!lunaredu::device::setup_tfcard_store(true)) {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.sd_ready = false;
    set_error_locked("sdcard_mount_failed");
  } else {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    rescan_locked();
  }

  // A reset must never immediately restore an audible/high-load state. This
  // also prevents a marginal amplifier rail from becoming a reboot/play loop.
  (void)dual_i2s_audio_mute(AUDIO_PLAYER_MUTE);

  // Pebble has no spectrum consumer. Do not start the legacy FFT task.
  (void)setup_smart_speaker_display();
  (void)setup_smart_speaker_sensors();
  ota_service_set_lifecycle_callback(ota_lifecycle, nullptr);

  setup_buttons();
  setup_encoder();
  (void)activate_device_capability(kCapability);
#if CONFIG_LUNAREDU_SMART_SPEAKER_AUTOPLAY_ON_BOOT
  enqueue_command(CommandType::kPlay);
#else
  ESP_LOGW(kTag,
           "safe startup: playback idle and muted; explicit play required "
           "(startup volume=%d)",
           kInitialVolume);
#endif
  return true;
}

void tick_runtime() {
  static uint32_t probe_at = 0;
  if (now_ms() - probe_at >= 3000) {
    probe_at = now_ms();
    const auto audio = dual_i2s_diagnostics();
    const auto touch = smart_speaker_touch_snapshot();
    ESP_LOGI(kTag, "[DEBUG-speaker-io] output peak0=%lu peak1=%lu submitted0=%llu submitted1=%llu completed0=%llu completed1=%llu mode=%s fault=%d rx=%d touch=%d/%lu error=%s",
             static_cast<unsigned long>(audio.probe_peak_i2s0), static_cast<unsigned long>(audio.probe_peak_i2s1),
             static_cast<unsigned long long>(audio.submitted_frames_left), static_cast<unsigned long long>(audio.submitted_frames_right),
             static_cast<unsigned long long>(audio.completed_frames_left), static_cast<unsigned long long>(audio.completed_frames_right),
             dsp::channel_mode_name(dual_i2s_audio_channel_mode()), audio.fault_muted, audio.microphone_input_enabled,
             touch.initialized, static_cast<unsigned long>(touch.sample_count), touch.last_error.c_str());
  }
  tick_smart_speaker_sensors();
  poll_encoder();
  poll_local_buttons();
  process_touch_gestures();
  if (g_touch_ptt_active && g_touch_ptt_release_pending) {
    const auto assistant = lunaredu::pet::conversation::runtime_snapshot();
    if (assistant.listening) {
      (void)xiaozhi_finish_listening();
      g_touch_ptt_active = false;
      g_touch_ptt_release_pending = false;
    } else if (!assistant.opening) {
      g_touch_ptt_active = false;
      g_touch_ptt_release_pending = false;
    }
  }
  dual_i2s_audio_tick();
  capture_dual_audio_debug_if_due();
  bool should_skip_decode_timeout = false;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    const uint32_t current_ms = now_ms();
    const auto assistant = lunaredu::pet::conversation::runtime_snapshot();
    const bool assistant_audio_active = assistant.opening || assistant.listening ||
                                        assistant.speaking;
    if (g_runtime.playing && !g_runtime.paused && !g_runtime.first_frame_seen &&
        !g_runtime.decode_timeout_skip_sent && !assistant_audio_active &&
        g_runtime.playlist.size() > 1 &&
        current_ms - g_runtime.play_started_ms >= kFirstFrameTimeoutMs) {
      g_runtime.decode_timeout_skip_sent = true;
      should_skip_decode_timeout = true;
      set_error_locked("track_decode_timeout");
    }
  }
  if (should_skip_decode_timeout) {
    ESP_LOGW(kTag, "first audio frame timeout, skipping track");
    queue_track_completion(true);
  }
  if (g_command_queue == nullptr) {
    return;
  }
  Command command;
  // Bound work per UI tick so input bursts cannot monopolize this executor.
  if (xQueueReceive(g_command_queue, &command, 0) == pdTRUE) {
    process_command(command);
  }
  const int delta = g_pending_volume_delta.exchange(0, std::memory_order_relaxed);
  if (delta != 0) process_command({CommandType::kAdjustVolume, delta, 0});
}

RuntimeStatus status_snapshot() {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  RuntimeStatus status;
  status.position_ms = dual_i2s_track_position_ms();
  status.has_lyrics = g_runtime.lyrics != nullptr;
  status.lyrics_delay_ms = g_runtime.lyrics_delay_ms;
  if (status.has_lyrics && (g_runtime.playing || g_runtime.paused))
    status.lyric_line = g_runtime.lyrics->line(status.position_ms, status.lyrics_delay_ms);
  status.playback_mode = g_runtime.playback_mode;
  status.mode_sequence = g_runtime.mode_sequence;
  status.mode_updated_ms = g_runtime.mode_updated_ms;
  status.sd_ready = g_runtime.sd_ready;
  status.audio_ready = g_runtime.audio_ready;
  status.playing = g_runtime.playing;
  status.paused = g_runtime.paused;
  status.uploading = g_runtime.uploading;
  status.volume = g_runtime.volume;
  status.current_index = g_runtime.current_index;
  status.playlist_count = g_runtime.playlist.size();
  if (g_runtime.current_index >= 0 &&
      g_runtime.current_index < static_cast<int>(g_runtime.playlist.size())) {
    const TrackInfo &track =
        g_runtime.playlist[static_cast<size_t>(g_runtime.current_index)];
    status.current_name = track.name;
    status.current_path = track.path;
  }
  status.last_error = g_runtime.last_error;
  status.eq_preset = dsp::preset_name(dual_i2s_audio_settings().preset);
  const dsp::Diagnostics diagnostics = dual_i2s_dsp_diagnostics();
  status.protection_active = diagnostics.protection_active ||
                             diagnostics.continuous_limiter_ms > 2000;
  return status;
}

std::vector<TrackInfo> playlist_snapshot() {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  return g_runtime.playlist;
}

void play() { enqueue_command(CommandType::kPlay); }
void pause() { enqueue_command(CommandType::kPause); }
void stop() { enqueue_command(CommandType::kStop); }
void next() { enqueue_command(CommandType::kNext); }
void previous() { enqueue_command(CommandType::kPrevious); }
void set_volume(int volume) { enqueue_command(CommandType::kSetVolume, volume); }
void adjust_volume(int delta) {
  enqueue_command(CommandType::kAdjustVolume, delta);
}
void rescan() { enqueue_command(CommandType::kRescan); }

LocalLibraryView local_library_snapshot() {
  std::lock_guard<std::mutex> menu_lock(g_menu_mutex);
  std::lock_guard<std::mutex> runtime_lock(g_runtime.mutex);
  sync_local_library_locked();
  return {g_runtime.playlist, g_menu.library_index, g_runtime.playlist_revision,
          g_runtime.sd_ready, g_runtime.uploading};
}

LocalMenuState local_menu_snapshot() {
  std::lock_guard<std::mutex> lock(g_menu_mutex);
  return g_menu;
}

uint32_t visual_activity_snapshot() {
  return g_visual_activity.load(std::memory_order_relaxed);
}

bool take_provisioning_request() {
  return g_provisioning_requested.exchange(false);
}

bool take_provisioning_cancel_request() {
  return g_provisioning_cancel_requested.exchange(false);
}

} // namespace smart_speaker
