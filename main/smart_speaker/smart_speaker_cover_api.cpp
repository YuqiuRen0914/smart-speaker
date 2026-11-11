#include "cover_api.h"
#include "runtime_internal.h"
#include "smart_speaker_cover.h"
#include "lunaredu_device_sdcard_tfcard_store.h"
#include "lunaredu_lunaros/device_api/device_projection_json.h"
#include <algorithm>
#include <cstring>

namespace smart_speaker {
using namespace detail;
namespace {
bool known_track(const std::string &path) {
  std::lock_guard lock(g_runtime.mutex);
  return std::any_of(g_runtime.playlist.begin(), g_runtime.playlist.end(),
      [&](const TrackInfo &track) { return track.path == path; });
}
void publish(const std::string &path, std::shared_ptr<const cover::Image> image) {
  std::lock_guard lock(g_runtime.mutex);
  for (auto &track : g_runtime.playlist) if (track.path == path) track.has_cover = image != nullptr;
  if (g_runtime.current_index >= 0 && size_t(g_runtime.current_index) < g_runtime.playlist.size() &&
      g_runtime.playlist[g_runtime.current_index].path == path) {
    g_runtime.cover_image = std::move(image);
    g_runtime.cover_error.clear();
  }
}
}

DeviceOperationResult handle_cover_upload(const DeviceBulkRequest &request, JsonObject response) {
  const std::string path = request.arg ? request.arg("path", request.context) : "";
  if (!known_track(path)) return device_operation_error("track_not_found", 404);
  const size_t size = std::max(request.content_length, request.body_size);
  if (size != cover::kBmpBytes) return device_operation_error("cover_size_invalid", 413);
  auto bmp = cover::allocate_bmp();
  if (!bmp) return device_operation_error("cover_memory_unavailable", 503);
  if (request.body_data && request.body_size == size) std::memcpy(bmp.get(), request.body_data, size);
  else if (request.read_chunk && request.content_length == size) {
    size_t offset = 0;
    while (offset < size) {
      const size_t wanted = std::min<size_t>(2048, size - offset);
      const int got = request.read_chunk(bmp.get() + offset, wanted, request.context);
      if (got <= 0 || size_t(got) > wanted) return device_operation_error("cover_read_failed", 400);
      offset += size_t(got);
    }
  } else return device_operation_error("cover_read_failed", 400);
  if (!cover::valid_bmp(bmp.get(), size)) return device_operation_error("cover_image_invalid", 400);
  auto image = cover::decode(bmp.get(), size);
  if (!image) return device_operation_error("cover_memory_unavailable", 503);
  // Receive and validate before locking: a stalled client must not monopolise
  // transport commands. Revalidate the authoritative association after I/O.
  std::unique_lock player(g_player_operation_mutex, std::try_to_lock);
  if (!player.owns_lock()) return device_operation_error("speaker_busy", 409);
  if (!lunaredu::device::tfcard_store_ready()) return device_operation_error("sdcard_not_ready", 503);
  if (!known_track(path)) return device_operation_error("track_not_found", 404);
  std::string error;
  if (!cover::save(path, bmp.get(), size, error)) return device_operation_error(error.c_str(), 500);
  publish(path, std::move(image));
  response["stored"] = true;
  response["path"] = path;
  response["has_cover"] = true;
  return device_operation_ack();
}

DeviceOperationResult handle_cover_remove(JsonVariantConst request, JsonObject response) {
  if (!request["path"].is<const char *>()) return device_operation_error("invalid_track_path", 400);
  const std::string path = request["path"].as<std::string>();
  std::unique_lock player(g_player_operation_mutex, std::try_to_lock);
  if (!player.owns_lock()) return device_operation_error("speaker_busy", 409);
  if (!lunaredu::device::tfcard_store_ready()) return device_operation_error("sdcard_not_ready", 503);
  if (!known_track(path)) return device_operation_error("track_not_found", 404);
  std::string error;
  if (!cover::remove(path, error)) return device_operation_error(error.c_str(), 500);
  publish(path, {});
  response["removed"] = true;
  response["path"] = path;
  return device_operation_ack();
}

DeviceOperationResult handle_cover_image(const DeviceMediaRequest &request, const DeviceMediaWriter &writer) {
  std::unique_lock player(g_player_operation_mutex, std::try_to_lock);
  if (!player.owns_lock()) return device_operation_error("speaker_busy", 409);
  if (!lunaredu::device::tfcard_store_ready()) return device_operation_error("sdcard_not_ready", 503);
  if (!known_track(request.path)) return device_operation_error("track_not_found", 404);
  auto bmp = cover::allocate_bmp();
  if (!bmp) return device_operation_error("cover_memory_unavailable", 503);
  std::string error;
  if (!cover::read(request.path, bmp.get(), cover::kBmpBytes, error)) return device_operation_error(error.c_str(), 404);
  player.unlock(); // Network send has no business holding the player lock.
  if (!writer.send_binary || !writer.send_binary(200, "image/bmp", bmp.get(), cover::kBmpBytes, writer.context))
    return device_operation_error("cover_send_failed", 500);
  return device_operation_ack();
}
}
