#include "runtime_internal.h"
#include "lyrics_api.h"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace lunaredu::lunaros {
// The tested handlers accept DeviceApiContext but do not read it. The host test
// supplies only that opaque parameter and exercises the real handler source.
struct DeviceApiContext {};
}  // namespace lunaredu::lunaros

namespace smart_speaker::detail {
Runtime g_runtime;
std::mutex g_player_operation_mutex;
}  // namespace smart_speaker::detail

using lunaredu::lunaros::DeviceBulkRequest;
using lunaredu::lunaros::DeviceApiContext;
using namespace smart_speaker;
using namespace smart_speaker::detail;

std::string upload(DeviceApiContext &device, const std::string &text,
                   const char *path = "song.mp3") {
  DeviceBulkRequest request;
  request.arg = [](const char *, void *context) {
    return std::string(static_cast<const char *>(context));
  };
  request.context = const_cast<char *>(path);
  request.body_size = text.size();
  request.body_data = reinterpret_cast<const uint8_t *>(text.data());
  JsonDocument response;
  return handle_lyrics_upload(device, request, response.to<JsonObject>()).error;
}

std::string offset(DeviceApiContext &device, int value) {
  JsonDocument request, response;
  request["path"] = "song.mp3";
  request["delay_ms"] = value;
  return handle_lyrics_offset(device, request.as<JsonVariantConst>(),
                              response.to<JsonObject>()).error;
}

struct Stream {
  std::string text;
  size_t at = 0;
  bool fail = false;
};

int read_chunk(uint8_t *out, size_t size, void *context) {
  auto &stream = *static_cast<Stream *>(context);
  if (stream.fail && stream.at > 0) return -1;
  size = std::min({size, size_t(3), stream.text.size() - stream.at});
  std::memcpy(out, stream.text.data() + stream.at, size);
  stream.at += size;
  return static_cast<int>(size);
}

int main() {
  DeviceApiContext device;
  g_runtime.playlist = {{"song", "song.mp3"}, {"other", "other.mp3"}};
  g_runtime.current_index = 0;

  assert(upload(device, "[00:01]原创测试\n[00:02]\n").empty());
  assert(g_runtime.playlist[0].has_lyrics && !g_runtime.playlist[1].has_lyrics);
  assert(g_runtime.lyrics->line(1000) == "原创测试");
  assert(upload(device, "bad") == "lyrics_timestamps_required");
  assert(lyrics::load("song.mp3")->line(1000) == "原创测试");
  assert(upload(device, "[00:01]x", "../escape.mp3") == "track_not_found");
  assert(offset(device, 500).empty() && g_runtime.lyrics_delay_ms == 500);
  assert(lyrics::delay("song.mp3") == 500 &&
         g_runtime.lyrics->line(1000, 500).empty());
  assert(offset(device, 10001) == "lyrics_offset_invalid");

  Stream stream{"[00:01]streamed\n"};
  DeviceBulkRequest request;
  request.content_length = stream.text.size();
  request.read_chunk = read_chunk;
  request.context = &stream;
  request.arg = [](const char *, void *) { return std::string("song.mp3"); };
  JsonDocument response;
  assert(handle_lyrics_upload(device, request, response.to<JsonObject>())
             .error.empty());
  assert(g_runtime.lyrics->line(1000) == "streamed");
  stream.at = 0;
  stream.fail = true;
  assert(handle_lyrics_upload(device, request, response.to<JsonObject>()).error ==
         "lyrics_read_failed");
  assert(lyrics::load("song.mp3")->line(1000) == "streamed");

  JsonDocument remove_request, remove_response;
  remove_request["path"] = "song.mp3";
  assert(handle_lyrics_remove(device, remove_request.as<JsonVariantConst>(),
                              remove_response.to<JsonObject>()).error.empty());
  assert(!lyrics::load("song.mp3") && !g_runtime.lyrics &&
         !g_runtime.playlist[0].has_lyrics);
  request.content_length = lyrics::kMaxBytes + 1;
  assert(handle_lyrics_upload(device, request, response.to<JsonObject>()).error ==
         "lyrics_size_invalid");
}
