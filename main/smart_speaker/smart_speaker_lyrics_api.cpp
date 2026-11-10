#include "lyrics_api.h"
#include "runtime_internal.h"
#include "device_api/device_operation_types.h"
#include <algorithm>
#include <cstring>
namespace smart_speaker {
using namespace lunaredu::lunaros;
using namespace detail;
bool lyric_track_exists(const std::string &path) {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  return std::any_of(g_runtime.playlist.begin(), g_runtime.playlist.end(),
      [&](const TrackInfo &t) { return t.path == path; });
}

void publish_lyrics(const std::string &path,
                    std::shared_ptr<const lyrics::Document> doc, int delay) {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  ++g_runtime.playlist_revision;
  for (auto &track : g_runtime.playlist) if (track.path == path) {
    track.has_lyrics = doc != nullptr;
    track.lyrics_delay_ms = delay;
  }
  if (g_runtime.current_index >= 0 && size_t(g_runtime.current_index) < g_runtime.playlist.size() &&
      g_runtime.playlist[g_runtime.current_index].path == path) {
    g_runtime.lyrics = std::move(doc);
    g_runtime.lyrics_delay_ms = delay;
  }
}

DeviceOperationResult handle_lyrics_upload(DeviceApiContext &, const DeviceBulkRequest &request, JsonObject response) {
  std::unique_lock<std::mutex> player_lock(g_player_operation_mutex, std::try_to_lock);
  if (!player_lock.owns_lock()) return device_operation_error("speaker_busy",409);
  const std::string path = (request.arg ? request.arg("path",request.context) : std::string());
  if (!lyric_track_exists(path)) return device_operation_error("track_not_found",404);
  const size_t size = std::max(request.content_length, request.body_size);
  if (!size || size > lyrics::kMaxBytes) return device_operation_error("lyrics_size_invalid",413);
  std::string text(size,'\0');
  if (request.body_data && request.body_size == size) {
    std::memcpy(text.data(),request.body_data,size);
  } else if (request.read_chunk && request.content_length == size) {
    size_t offset=0;
    while (offset<size) {
      const size_t wanted=std::min<size_t>(2048,size-offset);
      const int got=request.read_chunk(reinterpret_cast<uint8_t *>(text.data()+offset),wanted,request.context);
      if (got<=0 || size_t(got)>wanted) return device_operation_error("lyrics_read_failed",400);
      offset+=size_t(got);
    }
  } else return device_operation_error("lyrics_read_failed",400);
  auto doc=std::make_shared<lyrics::Document>(); std::string error;
  if (!lyrics::parse(text,*doc,error)) return device_operation_error(error.c_str(),400);
  if (!lyrics::save(lyrics::path_for(path),text)) return device_operation_error("lyrics_save_failed",500);
  const int delay=lyrics::delay(path);
  publish_lyrics(path,doc,delay);
  response["path"]=path;
  response["lines"]=doc->cues.size();
  response["lyrics_delay_ms"]=delay;
  return device_operation_ack();
}

DeviceOperationResult handle_lyrics_remove(DeviceApiContext &, JsonVariantConst request, JsonObject response) {
  if (!request["path"].is<const char *>()) return device_operation_error("invalid_track_path",400);
  std::unique_lock<std::mutex> player_lock(g_player_operation_mutex,std::try_to_lock);
  if (!player_lock.owns_lock()) return device_operation_error("speaker_busy",409);
  const std::string path=request["path"].as<std::string>();
  if (!lyric_track_exists(path)) return device_operation_error("track_not_found",404);
  const auto base=lyrics::path_for(path);
  // Remove recovery copies before the active version so a failed removal cannot
  // resurrect old lyrics after a reboot.
  for (const char *suffix : {".bak", ".upload", ".offset.bak", ".offset.upload", ".offset", ""}) {
    const auto file=base+suffix;
    if (lyrics::exists(file) && remove(file.c_str())!=0) return device_operation_error("lyrics_remove_failed",500);
  }
  publish_lyrics(path,{},0);
  response["removed"]=true;
  return device_operation_ack();
}

DeviceOperationResult handle_lyrics_offset(DeviceApiContext &, JsonVariantConst request, JsonObject response) {
  if (!request["path"].is<const char *>() || !request["delay_ms"].is<int>())
    return device_operation_error("lyrics_offset_invalid",400);
  const int delay=request["delay_ms"].as<int>();
  if (delay < -10000 || delay > 10000) return device_operation_error("lyrics_offset_invalid",400);
  std::unique_lock<std::mutex> player_lock(g_player_operation_mutex,std::try_to_lock);
  if (!player_lock.owns_lock()) return device_operation_error("speaker_busy",409);
  const std::string path=request["path"].as<std::string>();
  if (!lyric_track_exists(path)) return device_operation_error("track_not_found",404);
  const auto doc=lyrics::load(path);
  if (!doc) return device_operation_error("lyrics_not_found",404);
  if (!lyrics::save(lyrics::path_for(path)+".offset",std::to_string(delay)))
    return device_operation_error("lyrics_save_failed",500);
  publish_lyrics(path,doc,delay);
  response["lyrics_delay_ms"]=delay;
  return device_operation_ack();
}
}
