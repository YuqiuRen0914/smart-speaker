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
#include "smart_speaker_cover.h"
#include "smart_speaker_noise.h"
#include "smart_speaker_log_buffer.h"
#include "smart_speaker_sensors.h"
#include "smart_speaker_touch_routing.h"
#include "smart_speaker_focus_policy.h"
#include "smart_speaker_xiaozhi.h"
#include "lunaredu_pet_conversation.h"
#include "product/product_state_runtime.h"
#include "product/desktop_resources.h"
#include "product/weather.h"

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
std::mutex g_command_admission_mutex;
std::atomic<uint32_t> g_playback_intent_revision{0};
std::atomic<bool> g_assistant_focus_release_requested{false};

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
bool g_assistant_start_requested = false;
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
  uint64_t context = 0;
};
LocalButtonState g_local_left, g_local_right;
std::atomic<uint32_t> g_visual_activity{0};

bool changes_playback_intent(CommandType type) {
  return type == CommandType::kPlay || type == CommandType::kPlayTrack ||
      type == CommandType::kPause || type == CommandType::kStop ||
      type == CommandType::kNext || type == CommandType::kPrevious ||
      type == CommandType::kWhiteNoise;
}

bool enqueue_command(CommandType type, int value, uint32_t playlist_revision,
                     uint32_t focus_generation,
                     uint32_t intent_revision) {
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
    Command command;
    command.type = type;
    command.value = value;
    command.playlist_revision = playlist_revision;
    command.focus_generation = focus_generation;
    command.intent_revision = intent_revision;
    std::lock_guard<std::mutex> admission(g_command_admission_mutex);
    if (xQueueSend(g_command_queue, &command, 0) != pdTRUE) return false;
    if (changes_playback_intent(type))
      g_playback_intent_revision.fetch_add(1, std::memory_order_acq_rel);
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

void retry_assistant_focus_resume() {
  constexpr uint8_t kMaxResumeAttempts = 3;
  constexpr uint32_t kResumeRetryDelayMs = 100;
  Command command;
  const uint32_t now = now_ms();
  {
    std::lock_guard lock(g_runtime.mutex);
    if (!g_runtime.assistant_focus_resume_pending ||
        g_runtime.assistant_focus_resume_queued ||
        (g_runtime.assistant_focus_resume_retry_at_ms != 0 &&
         static_cast<int32_t>(now - g_runtime.assistant_focus_resume_retry_at_ms) < 0))
      return;
    if (g_runtime.assistant_focus_resume_attempts >= kMaxResumeAttempts) {
      g_runtime.assistant_focus_resume_pending = false;
      g_runtime.assistant_focus_resume_queued = false;
      g_runtime.assistant_focus_was_playing = false;
      set_error_locked("assistant_playback_restore_failed");
      return;
    }
    command.type = CommandType::kAssistantResume;
    command.focus_generation = g_runtime.assistant_focus_generation;
    command.intent_revision = g_runtime.assistant_focus_intent_revision;
    g_runtime.assistant_focus_resume_queued = true;
  }
  if (enqueue_command(command.type, 0, 0, command.focus_generation,
                      command.intent_revision)) return;
  std::lock_guard lock(g_runtime.mutex);
  if (g_runtime.assistant_focus_generation != command.focus_generation ||
      !g_runtime.assistant_focus_resume_pending) return;
  g_runtime.assistant_focus_resume_queued = false;
  ++g_runtime.assistant_focus_resume_attempts;
  g_runtime.assistant_focus_resume_retry_at_ms = now + kResumeRetryDelayMs;
}

void apply_assistant_focus_release_request() {
  {
    std::lock_guard lock(g_runtime.mutex);
    if (!g_assistant_focus_release_requested.exchange(false,
            std::memory_order_acq_rel)) return;
    g_runtime.assistant_focus_active = false;
    if (!playback_focus_group_unowned(g_runtime.assistant_focus_active,
                                      g_runtime.game_focus_active)) return;
    if (!g_runtime.assistant_focus_was_playing) return;
    g_runtime.assistant_focus_resume_pending = true;
    g_runtime.assistant_focus_resume_queued = false;
    g_runtime.assistant_focus_resume_attempts = 0;
    g_runtime.assistant_focus_resume_retry_at_ms = 0;
  }
  retry_assistant_focus_resume();
}

void sync_game_audio_focus() {
  const auto navigation = product::product_navigation_snapshot();
  const bool wants_focus = navigation.page == product::Page::Feature &&
      navigation.module == product::Module::Games;
  bool enqueue_pause = false;
  uint32_t generation = 0;
  uint32_t intent_revision = 0;
  {
    std::lock_guard lock(g_runtime.mutex);
    if (wants_focus && g_runtime.game_focus_failed) return;
    if (!wants_focus) g_runtime.game_focus_failed = false;
    if (wants_focus == g_runtime.game_focus_active) return;
    g_runtime.game_focus_active = wants_focus;
    if (wants_focus) {
      // A game can enter while an assistant resume is queued. The new owner
      // cancels that resume and inherits the existing pause snapshot. Keep
      // the generation so an already queued pause remains ordered before the
      // eventual resume after the game exits.
      if (g_runtime.assistant_focus_resume_pending) {
        g_runtime.assistant_focus_resume_pending = false;
        g_runtime.assistant_focus_resume_queued = false;
        g_runtime.assistant_focus_resume_attempts = 0;
        g_runtime.assistant_focus_resume_retry_at_ms = 0;
      } else if (!g_runtime.assistant_focus_active) {
        ++g_runtime.assistant_focus_generation;
        if (g_runtime.assistant_focus_generation == 0)
          ++g_runtime.assistant_focus_generation;
        g_runtime.assistant_focus_intent_revision =
            g_playback_intent_revision.load(std::memory_order_acquire);
        g_runtime.assistant_focus_was_playing = g_runtime.playing && !g_runtime.paused;
      }
      generation = g_runtime.assistant_focus_generation;
      intent_revision = g_runtime.assistant_focus_intent_revision;
      enqueue_pause = g_runtime.assistant_focus_was_playing &&
          !g_runtime.assistant_focus_active && !g_runtime.paused;
    } else if (!g_runtime.assistant_focus_active &&
               g_runtime.assistant_focus_was_playing) {
      g_assistant_focus_release_requested.store(true, std::memory_order_release);
    } else if (!g_runtime.assistant_focus_active) {
      g_runtime.assistant_focus_was_playing = false;
    }
  }
  if (enqueue_pause && !enqueue_command(CommandType::kAssistantPause, 0, 0,
                                        generation, intent_revision)) {
    std::lock_guard lock(g_runtime.mutex);
    if (g_runtime.game_focus_active &&
        g_runtime.assistant_focus_generation == generation) {
      g_runtime.game_focus_active = false;
      g_runtime.game_focus_failed = true;
      g_runtime.assistant_focus_was_playing = false;
      set_error_locked("game_playback_pause_queue_full");
    }
  }
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
    track.has_cover = cover::exists(track.path);
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

// Retry a transient startup timeout without touching an already mounted card.
// The player owner calls this before taking the playback-state mutex.
bool recover_sdcard_mount() {
  if (lunaredu::device::tfcard_store_ready()) return true;
  for (unsigned attempt = 0; attempt < 3; ++attempt) {
    if (attempt) vTaskDelay(pdMS_TO_TICKS(100));
    if (lunaredu::device::tfcard_store_mount()) return true;
  }
  return false;
}

void rescan_locked() {
  ++g_runtime.playlist_revision;
  if (!lunaredu::device::tfcard_store_ready()) {
    g_runtime.sd_ready = false;
    g_runtime.playlist.clear();
    g_runtime.current_index = -1;
    g_runtime.cover_image.reset();
    g_runtime.cover_error.clear();
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
      g_runtime.playlist[size_t(g_runtime.current_index)].path != current_path) {
    g_runtime.lyrics.reset();
    g_runtime.cover_image.reset();
    g_runtime.cover_error.clear();
  }
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
  std::string cover_error;
  const auto track_cover = cover::load(path, cover_error);
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
    g_runtime.cover_image = track_cover;
    g_runtime.cover_error = cover_error == "cover_not_found" ? "" : cover_error;
    g_runtime.current_index = index;
    g_runtime.white_noise = false;
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

bool play_white_noise() {
  {
    std::lock_guard lock(g_runtime.mutex);
    if (!g_runtime.audio_ready) { set_error_locked("audio_not_ready"); return false; }
  }
  FILE *file = noise::open();
  if (file == nullptr) {
    std::lock_guard lock(g_runtime.mutex);
    set_error_locked("noise_source_unavailable");
    return false;
  }
  (void)dual_i2s_audio_fade_to_mute(100);
  dual_i2s_track_clock_reset();
  {
    std::lock_guard lock(g_runtime.mutex);
    g_runtime.lyrics.reset();
    g_runtime.cover_image.reset();
    g_runtime.cover_error.clear();
    g_runtime.white_noise = true;
    g_runtime.playing = true;
    g_runtime.paused = false;
    g_runtime.paused_by_volume = false;
    g_runtime.auto_advance = true;
    g_runtime.advance_queued = false;
    g_runtime.first_frame_seen = false;
    g_runtime.decode_timeout_skip_sent = false;
    g_runtime.play_started_ms = now_ms();
    ++g_runtime.playback_generation;
    clear_error_locked();
  }
  if (audio_player_play(file) != ESP_OK) {
    fclose(file);
    std::lock_guard lock(g_runtime.mutex);
    g_runtime.playing = g_runtime.paused = g_runtime.auto_advance = false;
    set_error_locked("noise_play_failed");
    return false;
  }
  dual_i2s_audio_fade_up();
  return true;
}

void queue_track_completion(bool failed) {
  uint32_t generation = 0;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!g_runtime.auto_advance || !g_runtime.playing || g_runtime.uploading ||
        g_runtime.advance_queued || (!g_runtime.white_noise && g_runtime.playlist.empty())) return;
    failed = failed || !g_runtime.first_frame_seen;
    if (g_runtime.white_noise && failed) {
      g_runtime.playing = g_runtime.paused = g_runtime.auto_advance = false;
      set_error_locked("noise_decode_failed");
      return;
    }
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
  if (!player_lock.owns_lock()) {
    std::lock_guard lock(g_runtime.mutex);
    set_error_locked("speaker_busy");
    if (command.type == CommandType::kAssistantResume &&
        g_runtime.assistant_focus_generation == command.focus_generation &&
        g_runtime.assistant_focus_resume_pending) {
      g_runtime.assistant_focus_resume_queued = false;
      ++g_runtime.assistant_focus_resume_attempts;
      g_runtime.assistant_focus_resume_retry_at_ms = now_ms() + 100;
    }
    return;
  }
  // Explicit transport commands override a pause caused by muting, including
  // a second manual pause while already muted. Volume alone may resume only
  // the playback it paused; it must never start an idle or stopped player.
  if (command.type == CommandType::kPlay || command.type == CommandType::kPlayTrack ||
      command.type == CommandType::kPause || command.type == CommandType::kStop ||
      command.type == CommandType::kNext || command.type == CommandType::kPrevious ||
      command.type == CommandType::kRescan || command.type == CommandType::kWhiteNoise) {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.paused_by_volume = false;
  }
  switch (command.type) {
  case CommandType::kAssistantPause: {
    bool should_pause = false;
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      should_pause = (g_runtime.assistant_focus_active || g_runtime.game_focus_active) &&
          g_runtime.assistant_focus_generation == command.focus_generation &&
          g_runtime.assistant_focus_was_playing;
    }
    if (should_pause) {
      (void)dual_i2s_audio_fade_to_mute(100);
      if (audio_player_pause() == ESP_OK) update_play_state(false, true, true);
      else {
        std::lock_guard<std::mutex> lock(g_runtime.mutex);
        set_error_locked("assistant_playback_pause_failed");
      }
    }
    break;
  }
  case CommandType::kAssistantResume: {
    bool should_resume = false;
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      if (g_runtime.assistant_focus_generation != command.focus_generation ||
          !g_runtime.assistant_focus_resume_pending) break;
      const bool unchanged = g_playback_intent_revision.load(
          std::memory_order_acquire) == command.intent_revision;
      should_resume = assistant_focus_resume_allowed(
          g_runtime.assistant_focus_was_playing, unchanged,
          g_runtime.paused && !g_runtime.playing,
          g_runtime.paused_by_volume, g_runtime.volume);
      if (!should_resume) {
        g_runtime.assistant_focus_resume_pending = false;
        g_runtime.assistant_focus_resume_queued = false;
        g_runtime.assistant_focus_was_playing = false;
        g_runtime.assistant_focus_resume_attempts = 0;
        g_runtime.assistant_focus_resume_retry_at_ms = 0;
      }
    }
    if (!should_resume) break;
    if (audio_player_resume() == ESP_OK) {
      update_play_state(true, false, true);
      dual_i2s_audio_fade_up();
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      if (g_runtime.assistant_focus_generation == command.focus_generation) {
        g_runtime.assistant_focus_resume_pending = false;
        g_runtime.assistant_focus_resume_queued = false;
        g_runtime.assistant_focus_was_playing = false;
        g_runtime.assistant_focus_resume_attempts = 0;
        g_runtime.assistant_focus_resume_retry_at_ms = 0;
      }
    } else {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      if (g_runtime.assistant_focus_generation == command.focus_generation) {
        set_error_locked("assistant_playback_resume_failed");
        g_runtime.assistant_focus_resume_queued = false;
        ++g_runtime.assistant_focus_resume_attempts;
        g_runtime.assistant_focus_resume_retry_at_ms = now_ms() + 100;
      }
    }
    break;
  }
  case CommandType::kWhiteNoise:
    (void)play_white_noise();
    break;
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
    bool white_noise = false;
    int current_index = -1;
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      paused = g_runtime.paused;
      white_noise = g_runtime.white_noise;
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
    if (white_noise) (void)play_white_noise(); else (void)play_index(current_index);
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
    {
      std::lock_guard lock(g_runtime.mutex);
      g_runtime.white_noise = false;
    }
    update_play_state(false, false, false);
    break;
  case CommandType::kNext:
  case CommandType::kAutoNext:
  case CommandType::kSkipFailed: {
    int next_index = -1;
    bool repeat_noise = false;
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      const bool automatic = command.type != CommandType::kNext;
      if (automatic && (!g_runtime.auto_advance || !g_runtime.advance_queued || g_runtime.uploading || g_runtime.paused ||
                        command.playlist_revision != g_runtime.playback_generation)) break;
      g_runtime.advance_queued = false;
      repeat_noise = automatic && g_runtime.white_noise;
      if (!repeat_noise) {
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
    }
    if (repeat_noise) (void)play_white_noise();
    else if (next_index >= 0) (void)play_index(next_index);
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
    // Reload the persisted selection after recovery, without holding the
    // playback mutex while reading/decoding the wallpaper.
    if (recover_sdcard_mount()) (void)product::desktop_resources_load();
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

bool assistant_interaction_active() {
  const auto assistant = lunaredu::pet::conversation::runtime_snapshot();
  return assistant.interaction_active;
}

// A press belongs to the page on which it began. Never deliver its release to
// a newly opened page (including an asynchronously voice-woken conversation).
uint64_t local_input_context_locked() {
  return (uint64_t(product::product_navigation_snapshot().revision) << 16) |
      (uint64_t(assistant_interaction_active()) << 12) |
      (uint64_t(g_menu.product_shell) << 8) | (uint64_t(g_menu.open) << 7) |
      (uint64_t(g_menu.provisioning) << 6) | (uint64_t(g_menu.binding) << 5) |
      (uint64_t(g_menu.information) << 4) | (uint64_t(g_menu.network) << 3) |
      (uint64_t(g_menu.library) << 2) | uint64_t(g_menu.unbind_confirm);
}

void return_to_local_parent_locked() {
  g_menu.open = true;
  if (g_menu.settings_child) {
    g_menu.product_shell = true;
    g_menu.settings_child = false;
  }
}

void return_home_locked(NetworkProvisioningState provisioning) {
  if (g_menu.provisioning || provisioning != NetworkProvisioningState::Idle) {
    g_provisioning_requested.store(false);
    g_provisioning_cancel_requested.store(true);
    g_menu.provisioning_dismissed = true;
    g_provisioning_observed = false;
  }
  g_menu.open = g_menu.product_shell = g_menu.settings_child = false;
  g_menu.binding = g_menu.information = g_menu.network = g_menu.library = false;
  g_menu.provisioning = g_menu.unbind_confirm = false;
  g_menu.hold_percent = g_menu.mode_hold_percent = 0;
  g_assistant_start_requested = false;
  (void)product::product_navigation_input(product::Input::Home);
}

// Left short and touch tap share confirmation; right short goes to the parent.
bool route_local_menu_button_locked(bool confirm,
                                    NetworkProvisioningState provisioning,
                                    NetworkLinkState link) {
  if (g_menu.product_shell) {
    const auto state = product::product_state_snapshot(now_ms() / 1000, now_ms());
    if (confirm) {
      if (state.navigation.page == product::Page::PetActions) {
        if (product::pet_animation_catalog_ready()) {
          smart_speaker_display_request_pet_action(state.navigation.item == 0 ?
              product::kPetToggleSleep : int(state.navigation.item) - 1);
          (void)product::product_navigation_input(product::Input::Home);
          g_menu.product_shell = false;
          g_menu.open = false;
        }
        return true;
      } else if (state.navigation.page == product::Page::Settings) {
        // Reuse the actual settings screens owned by the local menu.
        constexpr int settings_rows[] = {0, 2, 1};
        g_menu.product_shell = false;
        g_menu.open = true;
        g_menu.selected = settings_rows[state.navigation.item];
        g_menu.settings_child = true;
        return route_local_menu_button_locked(true, provisioning, link);
      } else if (state.navigation.page == product::Page::Feature) {
        switch (state.navigation.module) {
          case product::Module::Music: {
            const auto status = status_snapshot();
            const bool noise_page = state.navigation.item == 1;
            if (status.white_noise == noise_page) {
              if (status.playing && !status.paused) pause(); else play();
            } else if (noise_page) (void)enqueue_command(CommandType::kWhiteNoise);
            else {
              std::lock_guard lock(g_runtime.mutex);
              (void)enqueue_command(CommandType::kPlayTrack, g_runtime.current_index < 0 ? 0 : g_runtime.current_index,
                                    g_runtime.playlist_revision);
            }
            break;
          }
          case product::Module::Assistant: {
            g_assistant_start_requested = true;
            break;
          }
          case product::Module::Time:
            if (state.navigation.item == 1) {
              const auto action = !state.pomodoro.active ? product::PomodoroAction::Start :
                  state.pomodoro.paused ? product::PomodoroAction::Resume : product::PomodoroAction::Pause;
              (void)product::product_request_pomodoro(action);
            }
            break;
          case product::Module::Games:
            if (state.navigation.item == 0) product::product_hit_wood_fish(now_ms());
            break;
          default: break;
        }
      }
      (void)product::product_navigation_input(product::Input::Confirm);
    } else {
      (void)product::product_navigation_input(product::Input::Back);
      if (product::product_state_snapshot(now_ms() / 1000, now_ms()).navigation.page == product::Page::Desktop) {
        g_menu.product_shell = false;
        g_menu.open = false;
      }
    }
    return true;
  }
  if (g_menu.provisioning || (!g_menu.provisioning_dismissed &&
                              provisioning != NetworkProvisioningState::Idle)) {
    if (!confirm) {
      g_provisioning_requested.store(false);
      g_provisioning_cancel_requested.store(true);
      g_menu.provisioning = false;
      g_menu.provisioning_dismissed = true;
      g_provisioning_observed = false;
      g_menu.open = false;
      g_menu.binding = false;
      g_menu.information = false;
      g_menu.network = true;
      g_menu.library = false;
      g_menu.selected = 0;
      g_menu.hold_percent = 0;
      g_local_right.consumed = true;
    }
    return true;
  }
  if (g_menu.network) {
    if (!confirm) {
      g_menu.network = false;
      return_to_local_parent_locked();
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
    if (!confirm) {
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
    if (!confirm) {
      g_menu.information = false;
      return_to_local_parent_locked();
    } else {
      g_menu.information_page = (g_menu.information_page + 1) % 3;
    }
    return true;
  }
  if (g_menu.binding) {
    if (!confirm) {
      if (g_menu.unbind_confirm) {
        g_menu.unbind_confirm = false;
        g_menu.account_selected = 1;
      } else {
        g_menu.binding = false;
        return_to_local_parent_locked();
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
    if (!confirm) {
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
  if (assistant_interaction_active() || g_assistant_start_requested) return true;
  if (g_menu.product_shell) {
    const auto navigation = product::product_navigation_snapshot();
    if (navigation.page == product::Page::Feature) {
      // The music feature shares the sole player volume owner. Games accept
      // touch only; other features have no rotary action yet.
      return navigation.module != product::Module::Music;
    }
    (void)product::product_navigation_input(delta < 0 ? product::Input::Up : product::Input::Down);
    return true;
  }
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

void sync_encoder_page_context() {
  static uint64_t previous = 0;
  uint64_t context;
  {
    std::lock_guard<std::mutex> lock(g_menu_mutex);
    const auto nav = product::product_navigation_snapshot();
    context = (local_input_context_locked() & 0xffffU) |
        (uint64_t(nav.page) << 16) | (uint64_t(nav.module) << 24) |
        (nav.page == product::Page::Feature ? uint64_t(nav.item) << 32 : 0);
  }
  if (context == previous) return;
  previous = context;
  // Discard quadrature accumulated for the old page; keep ISR ownership and
  // the per-tick processing budget intact. Selection changes stay responsive.
  portENTER_CRITICAL(&g_encoder_lock);
  g_encoder_delta = 0;
  portEXIT_CRITICAL(&g_encoder_lock);
}

void setup_buttons() {
  lunaredu::device::DeviceGpioButtonConfig previous;
  previous.label = "previous";
  previous.gpio = CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_BUTTON_GPIO;
  previous.active_high = !CONFIG_LUNAREDU_SMART_SPEAKER_BUTTON_ACTIVE_LOW;
  previous.short_press_time_ms = kButtonShortPressMs;
  previous.long_press_time_ms = 800;
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
  next_config.long_press_time_ms = 800;
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
        if (g_menu.settings_child) g_menu.network = true;
        g_provisioning_observed = false;
      }
    }
  }
  const int active = CONFIG_LUNAREDU_SMART_SPEAKER_BUTTON_ACTIVE_LOW ? 0 : 1;
  bool start_assistant = false, cancel_assistant = false;
  const bool both_down =
      gpio_get_level(static_cast<gpio_num_t>(CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_BUTTON_GPIO)) == active &&
      gpio_get_level(static_cast<gpio_num_t>(CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_BUTTON_GPIO)) == active;
  auto poll = [&](LocalButtonState &button, int pin, bool right) {
    const bool raw = gpio_get_level(static_cast<gpio_num_t>(pin)) == active;
    if (raw != button.raw) { button.raw = raw; button.changed_ms = now; }
    std::lock_guard<std::mutex> lock(g_menu_mutex);
    const uint64_t context = local_input_context_locked();
    if (button.down && button.context != context) button.consumed = true;
    if (raw != button.down && now - button.changed_ms >= 30) {
      button.down = raw;
      if (raw) {
        button.pressed_ms = now;
        button.context = context;
        button.consumed = both_down;
      } else if (!button.consumed) {
        button.consumed = true;
        if (assistant_interaction_active() || g_assistant_start_requested) {
          if (right) {
            g_assistant_start_requested = false;
            cancel_assistant = true;
          }
        } else if (route_local_menu_button_locked(!right, provisioning, link)) {
          // Page owns confirmation/back.
        } else enqueue_command(right ? CommandType::kNext : CommandType::kPrevious);
      }
    }
    if (both_down) {
      g_local_left.consumed = g_local_right.consumed = true;
      return;
    }
    // Raw release suppresses a hold during the 30 ms release debounce.
    if (raw && button.down && !button.consumed && now - button.pressed_ms >= 800) {
      button.consumed = true;
      if (right) {
        cancel_assistant = true;
        return_home_locked(provisioning);
      } else start_assistant = true;
    }
    // No hold prompt, progress or preliminary animation on any page.
    g_menu.hold_percent = g_menu.mode_hold_percent = 0;
  };
  poll(g_local_left, CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_BUTTON_GPIO, false);
  poll(g_local_right, CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_BUTTON_GPIO, true);
  // Focus acquisition and network work must run outside the menu mutex.
  if (cancel_assistant) (void)xiaozhi_stop_listening();
  else if (start_assistant) {
    if (lunaredu::pet::conversation::runtime_snapshot().activation_ready)
      (void)xiaozhi_start_listening();
    else (void)xiaozhi_start_activation();
  }
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
  static uint64_t previous_context = 0;
  uint64_t context;
  {
    std::lock_guard<std::mutex> lock(g_menu_mutex);
    context = local_input_context_locked();
  }
  if (context != previous_context) {
    smart_speaker_cancel_touch();
    previous_context = context;
  }
  const TouchGesture gesture = smart_speaker_take_touch_gesture();
  if (gesture == TouchGesture::None || assistant_interaction_active() ||
      g_assistant_start_requested) return;
  const auto menu = local_menu_snapshot();
  const auto navigation = product::product_navigation_snapshot();
  const bool menu_active = menu.open || menu.binding || menu.information ||
      menu.product_shell || menu.network || menu.library || menu.provisioning ||
      (!menu.provisioning_dismissed && network_provisioning_state() != NetworkProvisioningState::Idle);
  if (menu.product_shell && navigation.page == product::Page::Feature) {
    product::Input input = product::Input::Confirm;
    if (gesture == TouchGesture::SwipeUp) input = product::Input::Up;
    if (gesture == TouchGesture::SwipeDown) input = product::Input::Down;
    if (gesture == TouchGesture::SwipeLeft) input = product::Input::Left;
    if (gesture == TouchGesture::SwipeRight) input = product::Input::Right;
    int dx = 0, dy = 0;
    if (product::game_move_for_input(navigation, input, dx, dy)) {
      (void)product::product_move_2048(dx, dy);
      return;
    }
  }
  const auto touch_context = !menu_active ? TouchContext::Home :
      menu.product_shell && navigation.page == product::Page::ModuleList ? TouchContext::Launcher :
      menu.product_shell && navigation.page == product::Page::Feature ? TouchContext::Feature : TouchContext::Menu;
  switch (route_touch_gesture(gesture, touch_context)) {
    case TouchRoute::MenuConfirm: {
      const auto provisioning = network_provisioning_state();
      const auto link = network_link_state();
      std::lock_guard<std::mutex> lock(g_menu_mutex);
      (void)route_local_menu_button_locked(true, provisioning, link);
      break;
    }
    case TouchRoute::Next:
    case TouchRoute::Previous: {
      const bool next = route_touch_gesture(gesture, touch_context) == TouchRoute::Next;
      // Preserve the legacy encoder wiring convention in legacy child views.
      (void)route_local_encoder(menu.product_shell ? (next ? 1 : -1) : (next ? -1 : 1));
      break;
    }
    case TouchRoute::OpenProductShell: {
      std::lock_guard<std::mutex> lock(g_menu_mutex);
      g_menu.open = g_menu.product_shell = true;
      (void)product::product_navigation_input(product::Input::Confirm);
      break;
    }
    case TouchRoute::None: break;
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

bool assistant_playback_focus_acquire(uint32_t timeout_ms) {
  uint32_t generation = 0;
  uint32_t intent_revision = 0;
  bool was_playing = false;
  bool first_owner = false;
  {
    std::lock_guard lock(g_runtime.mutex);
    // A previous turn's deferred release must be consumed before another
    // turn can inherit this lease, otherwise that release could resume music
    // after the new microphone has opened.
    if (g_assistant_focus_release_requested.load(std::memory_order_acquire)) {
      set_error_locked("assistant_playback_release_pending");
      return false;
    }
    if (g_runtime.assistant_focus_active) return true;
    if (g_runtime.assistant_focus_resume_pending) {
      set_error_locked("assistant_playback_restore_pending");
      return false;
    }
    g_runtime.assistant_focus_active = true;
    first_owner = !g_runtime.game_focus_active;
    if (first_owner) {
      ++g_runtime.assistant_focus_generation;
      if (g_runtime.assistant_focus_generation == 0)
        ++g_runtime.assistant_focus_generation;
      g_runtime.assistant_focus_intent_revision =
          g_playback_intent_revision.load(std::memory_order_acquire);
      g_runtime.assistant_focus_was_playing = g_runtime.playing && !g_runtime.paused;
    }
    generation = g_runtime.assistant_focus_generation;
    intent_revision = g_runtime.assistant_focus_intent_revision;
    was_playing = g_runtime.assistant_focus_was_playing;
  }
  if (!was_playing) return true;
  if (first_owner && !enqueue_command(CommandType::kAssistantPause, 0, 0,
                                      generation, intent_revision)) {
    std::lock_guard lock(g_runtime.mutex);
    if (g_runtime.assistant_focus_generation == generation) {
      g_runtime.assistant_focus_active = false;
      g_runtime.assistant_focus_was_playing = false;
    }
    set_error_locked("assistant_playback_pause_queue_full");
    return false;
  }
  const uint32_t started = now_ms();
  do {
    {
      std::lock_guard lock(g_runtime.mutex);
      if (g_runtime.assistant_focus_generation != generation ||
          !g_runtime.assistant_focus_active) return false;
      if (g_runtime.paused && !g_runtime.playing) return true;
    }
    if (static_cast<uint32_t>(now_ms() - started) >= timeout_ms) break;
    vTaskDelay(pdMS_TO_TICKS(10));
  } while (true);
  {
    std::lock_guard lock(g_runtime.mutex);
    if (g_runtime.assistant_focus_generation == generation) {
      if (assistant_focus_release_after_acquire_timeout(was_playing)) {
        g_assistant_focus_release_requested.store(true,
                                                   std::memory_order_release);
      } else {
        g_runtime.assistant_focus_active = false;
        g_runtime.assistant_focus_was_playing = false;
      }
    }
    set_error_locked("assistant_playback_pause_timeout");
  }
  return false;
}

void assistant_playback_focus_release() {
  // This callback can run while the Pet session mutex is held. Defer all
  // speaker-state locking to tick_runtime to keep the lock order one-way.
  g_assistant_focus_release_requested.store(true, std::memory_order_release);
}

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
  if (!product::product_time_load(now_ms() / 1000))
    ESP_LOGE(kTag, "product time state unreadable; preserving stored record and disabling time writes");
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

  if (!recover_sdcard_mount()) {
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

  if (!product::desktop_resources_load())
    ESP_LOGW(kTag, "desktop resources unavailable; showing explicit fallback");
  if (!product::weather_load())
    ESP_LOGW(kTag, "weather configuration unavailable; retaining saved record");

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
  sync_encoder_page_context();
  poll_encoder();
  poll_local_buttons();
  process_touch_gestures();
  sync_encoder_page_context();
  sync_game_audio_focus();
  if (g_assistant_start_requested) {
    g_assistant_start_requested = false;
    if (lunaredu::pet::conversation::runtime_snapshot().activation_ready)
      (void)xiaozhi_start_listening();
    else (void)xiaozhi_start_activation();
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
        (g_runtime.white_noise || g_runtime.playlist.size() > 1) &&
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
  apply_assistant_focus_release_request();
  if (g_command_queue == nullptr) {
    return;
  }
  retry_assistant_focus_resume();
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
  status.white_noise = g_runtime.white_noise;
  status.paused = g_runtime.paused;
  status.uploading = g_runtime.uploading;
  status.volume = g_runtime.volume;
  status.current_index = g_runtime.white_noise ? -1 : g_runtime.current_index;
  status.playlist_count = g_runtime.playlist.size();
  if (g_runtime.white_noise) status.current_name = "白噪声";
  else if (g_runtime.current_index >= 0 &&
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

CoverSnapshot current_cover_snapshot() {
  std::lock_guard lock(g_runtime.mutex);
  return {g_runtime.cover_image, g_runtime.cover_error};
}

bool play() { return enqueue_command(CommandType::kPlay); }
bool pause() { return enqueue_command(CommandType::kPause); }
bool stop() { return enqueue_command(CommandType::kStop); }
bool next() { return enqueue_command(CommandType::kNext); }
bool previous() { return enqueue_command(CommandType::kPrevious); }
bool set_volume(int volume) { return enqueue_command(CommandType::kSetVolume, volume); }
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
