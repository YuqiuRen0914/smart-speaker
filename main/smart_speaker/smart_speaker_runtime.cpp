#include "product/speaker_desktop.h"
#include "lunaredu_music_backend.h"
#include "lunaros_audio_focus.h"
#include "lunaros_audio_library.h"
#include "configuration/settings.h"
#include "lunaredu_music_lyrics.h"
#include "smart_speaker_runtime.h"
#include "runtime_internal.h"
#include "transfer_internal.h"
#include "lunaros_resource.h"
#include "lunaros_platform_link.h"
#include "lunaros_provisioning.h"
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

#include "lunaros_audio_file_player.h"
#include <driver/gpio.h>
#include <esp_check.h>
#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "device_api/device_projection.h"
#include "model/device_model.h"
#include "lunaredu_device_sdcard_storage.h"
#include "lunaredu_device_sdcard_tfcard_store.h"
#include "lunaredu_ota.h"
#include "lunaros_network.h"
#include "sdkconfig.h"
#include "smart_speaker_audio.h"
#include "smart_speaker_sensors.h"

namespace smart_speaker {
using namespace lunaredu::lunaros;
namespace detail {

constexpr const char *kTag = "smart_speaker";

constexpr const char *kMusicDir = "/sdcard/music";

constexpr uint32_t kAudioDebugIntervalMs = 500;
constexpr uint32_t kEncoderCommandCooldownMs = 45;
constexpr uint32_t kEncoderIdleClearMs = 140;
constexpr uint32_t kEncoderEdgeDebounceMs = 1;
constexpr uint32_t kFirstFrameTimeoutMs = 3000;

const DeviceCapabilityDescriptor kCapability = {
    "smart_speaker",
    nullptr,
    0,
};
DeviceProjectionBinding g_projection;
Runtime g_runtime;
bool g_file_player_created = false;
std::atomic<bool> g_player_retiring{false};
std::atomic<bool> g_ota_maintenance{false};
std::atomic<bool> g_library_refresh_pending{false};

// Never block the UI task on a long SD upload.
std::mutex g_player_operation_mutex;
QueueHandle_t g_command_queue = nullptr;
portMUX_TYPE g_encoder_lock = portMUX_INITIALIZER_UNLOCKED;
volatile int32_t g_encoder_delta = 0;
volatile uint8_t g_encoder_state = 0;
volatile TickType_t g_encoder_last_edge_tick = 0;
uint32_t g_last_encoder_command_ms = 0;
int16_t g_desktop_encoder_position = 0;
// Volume input is a high-rate relative signal. Keep it out of the general
// playback queue and apply the accumulated delta once per runtime tick.
std::atomic<int32_t> g_pending_volume_delta{0};
lunaredu::audio::AudioHistory g_audio_history{kAudioDebugIntervalMs};
// Product supplies storage, board policy and the existing queue/decoder owner.
// Playback transitions, completion fences and checked stop live in shared Music.
lunaredu::music::PlaybackCoordinator g_playback_coordinator{g_runtime,g_runtime.mutex,
    {kMaxVolume,true,kFirstFrameTimeoutMs,100},[]{
  lunaredu::music::PlaybackPorts p;
  p.track_count=[](void *){return g_runtime.playlist.size();};
  p.now_ms=[](void *){return now_ms();};p.entropy=[](void *){return esp_random();};
  p.enqueue=[](void *,Command command){return enqueue_command(command.type,command.value,command.playlist_revision);};
  p.play_index=[](void *,int index){return play_index(index);};
  p.pause=[](void *){return file_player_pause()==ESP_OK;};
  p.resume=[](void *){return file_player_resume()==ESP_OK;};
  p.stop=[](void *){return file_player_stop()==ESP_OK;};
  p.idle=[](void *){return !g_file_player_created||file_player_state()==FilePlayerState::Idle;};
  p.discard_pending=[](void *){
    g_pending_volume_delta.store(0, std::memory_order_relaxed);
    if(g_command_queue){Command ignored;while(xQueueReceive(g_command_queue,&ignored,0)==pdTRUE){}}
  };
  p.fade_to_mute=[](void *,uint32_t ms){return dual_i2s_audio_fade_to_mute(ms);};
  p.fade_up=[](void *){dual_i2s_audio_fade_up();};
  p.set_volume=[](void *,int volume){dual_i2s_audio_set_volume(volume);ESP_LOGI(kTag,"volume=%d max=%d",volume,kMaxVolume);};
  p.refresh=[](void *){g_library_refresh_pending=true;};
  p.persist_mode=[](void *,PlaybackMode mode){
    std::string error;
    const bool saved=settings_write_u8("spkr_player","mode",uint8_t(mode),error);
    if(saved)ESP_LOGI(kTag,"playback mode=%s",playback_mode_name(mode));
    return saved;
  };
  p.error=[](void *,const char *error){ESP_LOGW(kTag,"%s",error);};
  return p;
}()};
bool enqueue_command(CommandType type, int value, uint32_t playlist_revision) {
  if(g_player_retiring.load(std::memory_order_acquire)||g_ota_maintenance.load(std::memory_order_acquire))return false;
  if (g_command_queue == nullptr) {
    return false;
  }
  if (type == CommandType::kAdjustVolume) {
    if (value != 0) {
      g_pending_volume_delta.fetch_add(value, std::memory_order_relaxed);
    }
    return true;
  }
  const Command command{type, value, playlist_revision};
  if (xQueueSend(g_command_queue, &command, 0) != pdTRUE) return false;
  return true;
}

uint32_t now_ms() {
  return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

int clamp_volume(int volume) { return std::clamp(volume, 0, kMaxVolume); }

void capture_dual_audio_debug_if_due(){
  const auto now=now_ms();if(!g_audio_history.due(now))return;
  int volume;
  {std::lock_guard<std::mutex> lock(g_runtime.mutex);volume=g_runtime.volume;}
  g_audio_history.capture(now,volume);
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

void append_directory_tracks(const char *directory,std::vector<TrackInfo> &tracks){
  std::vector<lunaredu::audio::AudioLibraryEntry> files;std::string error;
  if(!lunaredu::audio::append_audio_directory(directory,lunaredu::audio::AudioFileFormat::Mp3,files,error))return;
  for(auto &file:files){
    TrackInfo track;track.name=std::move(file.name);track.path=std::move(file.path);track.size_bytes=file.size_bytes;
    track.has_lyrics=lyrics::exists(lyrics::readable(lyrics::path_for(track.path)));
    track.lyrics_delay_ms=lyrics::delay(track.path);tracks.push_back(std::move(track));
  }
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

void publish_imported_track(const std::string &path,size_t size) {
  TrackInfo track;track.path=path;track.name=path.substr(path.find_last_of('/')+1);track.size_bytes=size;
  track.has_lyrics=lyrics::exists(lyrics::readable(lyrics::path_for(path)));
  track.lyrics_delay_ms=lyrics::delay(path);
  const auto name=lower_copy(track.name);
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  auto &tracks=g_runtime.playlist;
  const auto at=std::lower_bound(tracks.begin(),tracks.end(),name,
      [](const TrackInfo &a,const std::string &b){return lower_copy(a.name)<b;});
  const auto index=static_cast<int>(at-tracks.begin());
  tracks.insert(at,std::move(track));
  if(g_runtime.current_index<0)g_runtime.current_index=0;
  else if(index<=g_runtime.current_index)++g_runtime.current_index;
  g_runtime.sd_ready=true;++g_runtime.playlist_revision;
}

void rescan_library() {
  const bool ready=lunaredu::device::tfcard_store_ready();
  std::vector<TrackInfo> tracks;
  if(ready){(void)ensure_music_dir();tracks=scan_tracks();}
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  ++g_runtime.playlist_revision;
  if (!ready) {
    g_runtime.sd_ready = false;
    g_runtime.playlist.clear();
    g_runtime.current_index = -1;
    set_error_locked("sdcard_not_ready");
    return;
  }

  g_runtime.sd_ready = true;
  const std::string current_path =
      g_runtime.current_index >= 0 &&
              g_runtime.current_index <
                  static_cast<int>(g_runtime.playlist.size())
          ? g_runtime.playlist[static_cast<size_t>(g_runtime.current_index)].path
          : std::string();
  g_runtime.playlist = std::move(tracks);
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

esp_err_t i2s_write_scaled(const void *audio_buffer, size_t len,
                           size_t *bytes_written, uint32_t timeout_ms,
                           void *ctx) {
  if(g_player_retiring.load(std::memory_order_acquire)||g_ota_maintenance.load(std::memory_order_acquire)){
    if(bytes_written)*bytes_written=len;
    return ESP_OK;
  }
  const esp_err_t err = dual_i2s_audio_write(audio_buffer, len, bytes_written,
                                              timeout_ms, ctx);
  if (err == ESP_OK && bytes_written != nullptr && *bytes_written > 0) {
    g_playback_coordinator.first_frame();
  }
  return err;
}

esp_err_t i2s_reconfig_clock(uint32_t rate, uint32_t bits_cfg,
                             unsigned channels) {
  if(g_player_retiring.load(std::memory_order_acquire)||g_ota_maintenance.load(std::memory_order_acquire))return ESP_OK;
  return dual_i2s_audio_reconfigure(rate, bits_cfg, channels);
}

esp_err_t audio_mute(AudioMute setting) {
  if(setting!=AudioMute::Mute&&(g_player_retiring.load(std::memory_order_acquire)||g_ota_maintenance.load(std::memory_order_acquire)))return ESP_OK;
  // UNMUTE is dispatched by the decoder exactly once at each file boundary,
  // including consecutive tracks of the same sample rate. Resume does not reset.
  if (setting == AudioMute::Unmute) dual_i2s_track_clock_reset();
  ESP_LOGI(kTag, "audio mute=%d", static_cast<int>(setting));
  return dual_i2s_audio_mute(setting);
}

void handle_audio_event(FilePlayerEventContext *);
bool setup_audio(lunaredu::lunaros::KernelRuntime &runtime) {
  if (!setup_speaker_audio_output(runtime)) return false;
  FilePlayerConfig config;
  config.context=&g_runtime;config.write=i2s_write_scaled;
  config.reconfigure=[](uint32_t rate,unsigned channels){return i2s_reconfig_clock(rate,16,channels);};
  config.mute=audio_mute;config.drain=dual_i2s_audio_drain;
  config.pause_output=[](bool paused){return dual_i2s_audio_mute(paused?AudioMute::Mute:AudioMute::Unmute);};
  config.discard=dual_i2s_audio_discard;
  config.event=handle_audio_event;config.priority=8;config.core=1;config.force_stereo=true;
  config.task_id="speaker.decode";config.service_id="speaker.audio";
  config.focus=&lunaredu::audio::local_audio_focus();
  config.effects={.compressor=true,.gain_db=0,.fade_in_ms=40};
  std::string error;
  if(!start_file_player(runtime,config,error)){
    ESP_LOGE(kTag,"file player init failed: %s",error.c_str());
    (void)stop_dual_i2s_audio(runtime);return false;
  }
  g_file_player_created=true;return true;
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
    // file_player_play(), so resetting first_frame_seen afterwards loses that
    // event and can cause a false first-frame timeout/skip.
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.lyrics = track_lyrics;
    g_runtime.lyrics_delay_ms = lyric_delay;
    g_playback_coordinator.begin_track_locked(index,now_ms());
  }
  const esp_err_t err = file_player_play(file);
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

void handle_audio_event(FilePlayerEventContext *ctx){
  if(!ctx)return;
  using lunaredu::music::PlaybackEvent;
  switch(ctx->event){
    case FilePlayerEvent::Playing:g_playback_coordinator.on_event(PlaybackEvent::Playing);break;
    case FilePlayerEvent::Paused:g_playback_coordinator.on_event(PlaybackEvent::Paused);break;
    case FilePlayerEvent::Idle:g_playback_coordinator.on_event(PlaybackEvent::Idle);break;
    case FilePlayerEvent::Failed:g_playback_coordinator.on_event(PlaybackEvent::Failed);break;
  }
}
// Dequeue and execution share the product operation lock with file mutations.
void process_command(const Command &command){g_playback_coordinator.execute(command);}

void process_pending_volume() {
  const int32_t pending = g_pending_volume_delta.exchange(0, std::memory_order_relaxed);
  if (pending == 0) return;
  const int delta = pending > std::numeric_limits<int>::max()
                       ? std::numeric_limits<int>::max()
                       : pending < std::numeric_limits<int>::min()
                             ? std::numeric_limits<int>::min()
                             : static_cast<int>(pending);
  process_command({CommandType::kAdjustVolume, delta, 0});
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
  static constexpr int8_t kTransition[16] = {
      0, -1, 1, 0,
      1, 0, 0, -1,
      -1, 0, 0, 1,
      0, 1, -1, 0,
  };
  const TickType_t now_tick = xTaskGetTickCountFromISR();
  portENTER_CRITICAL_ISR(&g_encoder_lock);
  if (g_encoder_last_edge_tick != 0 &&
      now_tick - g_encoder_last_edge_tick <
          pdMS_TO_TICKS(kEncoderEdgeDebounceMs)) {
    // Keep the state aligned with the pins while suppressing mechanical
    // bounce. This prevents an interrupt storm from becoming false steps.
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
    g_desktop_encoder_position = static_cast<int16_t>(uint16_t(g_desktop_encoder_position) + delta);
  }
}

bool setup_buttons() {
  gpio_config_t config{};
  config.pin_bit_mask=(1ULL<<CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_BUTTON_GPIO)|(1ULL<<CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_BUTTON_GPIO);
  config.mode=GPIO_MODE_INPUT;
  config.pull_up_en=CONFIG_LUNAREDU_SMART_SPEAKER_BUTTON_ACTIVE_LOW?GPIO_PULLUP_ENABLE:GPIO_PULLUP_DISABLE;
  config.pull_down_en=CONFIG_LUNAREDU_SMART_SPEAKER_BUTTON_ACTIVE_LOW?GPIO_PULLDOWN_DISABLE:GPIO_PULLDOWN_ENABLE;
  config.intr_type=GPIO_INTR_DISABLE;
  return gpio_config(&config)==ESP_OK;
}

void ota_lifecycle(OtaServiceLifecycleEvent event, void *) {
  if (event == OtaServiceLifecycleEvent::UploadStarting ||
      event == OtaServiceLifecycleEvent::UploadAborted ||
      event == OtaServiceLifecycleEvent::RestartRequested) {
    // Lifecycle callbacks must not block; the output task advances the ramp.
    (void)dual_i2s_audio_mute(AudioMute::Mute);
  }
}

// These handlers share the player-operation lock with uploads, removal and
// track changes. Disk access never holds the UI snapshot mutex.

} // namespace detail
using namespace detail;

bool stop_audio_service(lunaredu::lunaros::KernelRuntime &runtime,
                        std::string &error) {
  std::lock_guard<std::mutex> operation(g_player_operation_mutex);
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.audio_ready = false;
    g_runtime.playing = g_runtime.paused = false;
  }
  if (g_file_player_created) {
    if (!stop_file_player(error)) return false;
    g_file_player_created = false;
  }
  if(!lunaredu::audio::local_audio_focus().stop([](void *p,std::string &e){
      if(!stop_dual_i2s_audio(*static_cast<lunaredu::lunaros::KernelRuntime *>(p))){e="audio_release_pending";return false;}
      e.clear();return true;},&runtime,error))return false;
  error.clear();
  return true;
}

bool start_audio_service(lunaredu::lunaros::KernelRuntime &runtime,
                         std::string &error) {
  if (status_snapshot().audio_ready) return true;
  if (!setup_audio(runtime)) {
    std::string cleanup;
    (void)stop_audio_service(runtime, cleanup);
    error = cleanup.empty() ? "audio_init_failed" : cleanup;
    return false;
  }
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  g_runtime.audio_ready = true;
  error.clear();
  return true;
}

bool setup_runtime(lunaredu::lunaros::KernelRuntime &runtime) {
  if (!status_snapshot().audio_ready) return false;
  if (g_command_queue) return true;
  if(!setup_buttons())return false;
  uint8_t mode=0;std::string settings_error;
  if(lunaredu::lunaros::settings_read_u8("spkr_player","mode",mode,settings_error)==lunaredu::lunaros::SettingsReadResult::Found && mode<=2)
    g_runtime.playback_mode=static_cast<PlaybackMode>(mode);
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
    rescan_library();
  }

  // A reset must never immediately restore an audible/high-load state. This
  // also prevents a marginal amplifier rail from becoming a reboot/play loop.
  (void)dual_i2s_audio_mute(AudioMute::Mute);

  (void)setup_smart_speaker_sensors();
  ota_service_set_lifecycle_callback(ota_lifecycle, nullptr);
  ota_service_set_maintenance_callback([](bool entering,std::string &error,void *){
    g_ota_maintenance.store(entering,std::memory_order_release);
    if(!entering){error.clear();return true;}
    const uint32_t started=now_ms();
    do{
      if(stop_music_checked(error))return true;
      vTaskDelay(pdMS_TO_TICKS(10));
    }while(now_ms()-started<1000);
    g_ota_maintenance.store(false,std::memory_order_release);
    return false;
  });

  setup_encoder();
  (void)g_projection.activate(kCapability);
#if !CONFIG_LUNAREDU_SMART_SPEAKER_AUTOPLAY_ON_BOOT
  ESP_LOGW(kTag,
           "safe startup: playback idle and muted; explicit play required "
           "(startup volume=%d)",
           kInitialVolume);
#endif
  return true;
}

// Reuse the existing lower-priority product I/O executor for filesystem walks.
// Requests coalesce; a busy upload keeps the refresh pending without blocking UI.
void poll_library_refresh(){
  if(!g_library_refresh_pending.load())return;
  std::unique_lock<std::mutex> operation(g_player_operation_mutex,std::try_to_lock);
  if(!operation.owns_lock())return;
  if(g_player_retiring.load()||g_ota_maintenance.load()){g_library_refresh_pending=false;return;}
  if(g_library_refresh_pending.exchange(false))rescan_library();
}

void tick_runtime() {
  if(g_player_retiring.load(std::memory_order_acquire)){
    std::string error;(void)stop_music_checked(error);
  }
  tick_smart_speaker_sensors();
  poll_encoder();
  dual_i2s_audio_tick();
  capture_dual_audio_debug_if_due();
  g_playback_coordinator.tick();
  if (g_command_queue == nullptr) {
    return;
  }
  std::unique_lock<std::mutex> operation(g_player_operation_mutex,std::try_to_lock);
  if(!operation.owns_lock())return;
  // One potentially fading/SD operation per UI tick; preserve FIFO and buffers.
  Command command;
  if (xQueueReceive(g_command_queue, &command, 0) == pdTRUE) process_command(command);
  process_pending_volume();
}

namespace {
RuntimeStatus status_locked() {
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
  status.uploading = g_runtime.transfer_active;
  status.volume = g_runtime.volume;
  status.current_index = g_runtime.current_index;
  status.playlist_count = g_runtime.playlist.size();
  status.playlist_revision = g_runtime.playlist_revision;
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
}
RuntimeStatus status_snapshot(){std::lock_guard<std::mutex> lock(g_runtime.mutex);return status_locked();}

std::vector<TrackInfo> playlist_snapshot() {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  return g_runtime.playlist;
}

void play() { enqueue_command(CommandType::kPlay); }
void pause() { enqueue_command(CommandType::kPause); }
void stop() { enqueue_command(CommandType::kStop); }
void next() { enqueue_command(CommandType::kNext); }
void previous() { enqueue_command(CommandType::kPrevious); }
bool request_track_play(const std::string &path, std::string &error) {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if (g_runtime.transfer_active) {
    error = "upload_in_progress";
    return false;
  }
  if (!g_runtime.audio_ready) {
    error = "audio_not_ready";
    return false;
  }
  const auto track = std::find_if(
      g_runtime.playlist.begin(), g_runtime.playlist.end(),
      [&path](const TrackInfo &item) { return item.path == path; });
  if (track == g_runtime.playlist.end()) {
    error = "track_not_found";
    return false;
  }
  if (!enqueue_command(
          CommandType::kPlayTrack,
          static_cast<int>(std::distance(g_runtime.playlist.begin(), track)),
          g_runtime.playlist_revision)) {
    error = "command_queue_full";
    return false;
  }
  error.clear();
  return true;
}
void set_volume(int volume) { enqueue_command(CommandType::kSetVolume, volume); }
void adjust_volume(int delta) {
  enqueue_command(CommandType::kAdjustVolume, delta);
}
void rescan() { enqueue_command(CommandType::kRescan); }


DesktopInput desktop_input() {
  DesktopInput result;
  const auto pressed=[](int gpio){return gpio>=0 && (gpio_get_level(static_cast<gpio_num_t>(gpio))==0)==bool(CONFIG_LUNAREDU_SMART_SPEAKER_BUTTON_ACTIVE_LOW);};
  result.buttons=uint16_t((pressed(CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_BUTTON_GPIO)?1:0)|(pressed(CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_BUTTON_GPIO)?2:0));
  result.encoder_position=g_desktop_encoder_position;return result;
}
bool configure_music_backend(std::string &error) {
  using namespace lunaredu::music;
  static std::shared_ptr<const Library> library;
  static uint32_t revision=~uint32_t(0);
  return configure_player_backend({nullptr,
    [](void *){
      MusicSnapshot result;
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      const auto &current=g_runtime;
      const auto position=dual_i2s_track_position_ms();
      {
        if(!library||revision!=g_runtime.playlist_revision){
          auto value=std::make_shared<Library>();value->reserve(g_runtime.playlist.size());
          for(const auto &track:g_runtime.playlist)value->push_back({track.path,track.name,true});
          library=std::move(value);revision=g_runtime.playlist_revision;
        }
      }
      result.library=library;result.selected=current.current_index<0?0:size_t(current.current_index);
      result.storage_ready=current.sd_ready;result.audio_ready=current.audio_ready;
      result.playing=current.playing||current.paused;result.paused=current.paused;result.volume=uint8_t(current.volume);
      result.stopping=current.transfer_active||g_player_retiring.load(std::memory_order_acquire);
      result.position_seconds=position/1000;
      if(current.lyrics&&(current.playing||current.paused))result.lyric=current.lyrics->line(position,current.lyrics_delay_ms);
      result.playback_mode=playback_mode_label(current.playback_mode);
      result.library_hint="SD 卡 music · MP3 / 同名 LRC";
      std::snprintf(result.error.data(),result.error.size(),"%s",current.last_error.c_str());return result;
    },
    [](void *,std::string &e){
      if(!enqueue_command(CommandType::kRescan)){e="music_command_unavailable";return false;}
      e.clear();return true;
    },
    [](void *,const lunaredu::music::Track &track,std::string &e){return request_track_play(track.path,e);},
    [](void *,uint32_t id,std::string &e){
      std::unique_lock<std::mutex> operation(g_player_operation_mutex,std::try_to_lock);
      if(!operation.owns_lock()){e="music_transfer_pending";return false;}
      if(g_player_retiring.load(std::memory_order_acquire)||g_ota_maintenance.load(std::memory_order_acquire)){e="music_stop_pending";return false;}
      CommandType command;int value=0;
      switch(id){
        case 218:{const auto state=music_activity();command=state.playing&&!state.paused?CommandType::kPause:CommandType::kPlay;break;}
        case 219:command=CommandType::kNext;break;
        case 223:command=CommandType::kPrevious;break;
        case 224:command=CommandType::kAdjustVolume;value=-CONFIG_LUNAREDU_SMART_SPEAKER_VOLUME_STEP;break;
        case 220:command=CommandType::kAdjustVolume;value=CONFIG_LUNAREDU_SMART_SPEAKER_VOLUME_STEP;break;
        case 227:command=CommandType::kStop;break;
        case 226:command=CommandType::kRescan;break;
        case 228:command=CommandType::kCycleMode;break;
        default:e="music_action_unsupported";return false;
      }
      if(!enqueue_command(command,value)){e="music_command_unavailable";return false;}
      e.clear();return true;
    },
    [](void *,std::string &e){return stop_music_checked(e);},true,
    [](void *){
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      return PlaybackActivity{g_runtime.audio_ready,g_runtime.playing||g_runtime.paused,g_runtime.paused,
          g_runtime.transfer_active||g_player_retiring.load(std::memory_order_acquire)};
    }},error);
}
bool stop_music_checked(std::string &error) {
  std::unique_lock<std::mutex> lock(g_player_operation_mutex,std::try_to_lock);
  if(!lock.owns_lock()){error="music_transfer_pending";return false;}
  return detail::stop_music_locked(error);
}
bool detail::stop_music_locked(std::string &error) {
  g_player_retiring.store(true,std::memory_order_release);
  g_library_refresh_pending=false;
  if(!g_playback_coordinator.stop_checked(error))return false;
  g_player_retiring.store(false,std::memory_order_release);return true;
}

} // namespace smart_speaker
