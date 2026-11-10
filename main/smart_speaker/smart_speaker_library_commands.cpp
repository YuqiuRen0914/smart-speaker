#include "runtime_internal.h"
#include "smart_speaker_audio.h"
#include "lunaredu_device_sdcard_tfcard_store.h"
#include "device_api/device_operation_types.h"
#include <freertos/task.h>
#include <algorithm>
namespace smart_speaker {
using namespace lunaredu::lunaros;
using namespace detail;
DeviceOperationResult handle_track_remove_payload(DeviceApiContext &device, JsonVariantConst request,
                                                  JsonObject response) {
  if (!request["path"].is<const char *>())
    return device_operation_error("invalid_track_path", 400);
  const std::string path = request["path"].as<std::string>();
  std::unique_lock<std::mutex> player_lock(g_player_operation_mutex, std::try_to_lock);
  if (!player_lock.owns_lock()) return device_operation_error("speaker_busy", 409);
  if (!lunaredu::device::tfcard_store_ready())
    return device_operation_error("sdcard_not_ready", 503);
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (g_runtime.transfer_active) return device_operation_error("upload_in_progress", 409);
    // Only a scanned MP3 can be removed; never accept an arbitrary filesystem path.
    const auto track = std::find_if(g_runtime.playlist.begin(), g_runtime.playlist.end(),
        [&path](const TrackInfo &item) { return item.path == path; });
    if (track == g_runtime.playlist.end()) return device_operation_error("track_not_found", 404);
  }
  bool current = false, resume = false;
  int removed_index = -1;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    for (size_t i = 0; i < g_runtime.playlist.size(); ++i)
      if (g_runtime.playlist[i].path == path) removed_index = static_cast<int>(i);
    current = removed_index == g_runtime.current_index;
    resume = current && g_runtime.playing;
    if (current) {
      g_runtime.auto_advance = false;
      g_runtime.paused_by_volume = false;
    }
  }
  if (current) {
    (void)dual_i2s_audio_fade_to_mute(100);
    // Shared player cancels a paused decoder directly and acknowledges Idle
    // only after closing its FILE and retiring both physical output lanes.
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      g_runtime.auto_advance = false;
    }
    if (file_player_stop() != ESP_OK) {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      g_runtime.auto_advance = resume;
      return device_operation_error("playback_stop_failed", 503);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    const uint32_t started = now_ms();
    while (file_player_state() != FilePlayerState::Idle && now_ms() - started < 1500)
      vTaskDelay(pdMS_TO_TICKS(5));
    if (file_player_state() != FilePlayerState::Idle)
      return device_operation_error("playback_stop_timeout", 503);
  }
  const bool removed = remove(path.c_str()) == 0;
  if (removed) {
    const auto lyric_path=lyrics::path_for(path);
    for (const char *suffix : {"", ".bak", ".upload", ".offset", ".offset.bak", ".offset.upload"})
      (void)remove((lyric_path+suffix).c_str());
  }
  int next_index = -1;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (removed) {
      // Do not rescan the entire card or stop the decoder for an unrelated file.
      g_runtime.playlist.erase(g_runtime.playlist.begin() + removed_index);
      ++g_runtime.playlist_revision;
      if (current) {
        g_runtime.lyrics.reset();
        g_runtime.playing = g_runtime.paused = false;
        g_runtime.current_index = g_runtime.playlist.empty() ? -1 :
            removed_index % static_cast<int>(g_runtime.playlist.size());
        if (resume) next_index = g_runtime.current_index;
      } else if (removed_index < g_runtime.current_index) --g_runtime.current_index;
      clear_error_locked();
    } else {
      set_error_locked("track_delete_failed");
      if (resume) next_index = g_runtime.current_index;
    }
  }
  const bool continued = next_index >= 0 && play_index(next_index);
  if (!removed) return device_operation_error("track_delete_failed", 500);
  response["playback_continued"] = continued;
  response["removed"] = true;
  response["path"] = path;
  fill_status_payload(device, response["status"].to<JsonObject>());
  return device_operation_ack();
}
}
