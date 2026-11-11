#include "cover_api.h"
#include "runtime_internal.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>

using namespace smart_speaker;
using namespace smart_speaker::detail;
namespace smart_speaker::detail { Runtime g_runtime; std::mutex g_player_operation_mutex; }
bool sd_ready = true;
namespace lunaredu::device { bool tfcard_store_ready() { return sd_ready; } }

struct Upload {
  std::string path;
  std::vector<uint8_t> bytes;
  size_t offset = 0;
  size_t stop_at = SIZE_MAX;
  bool remove_track = false;
};
std::string argument(const char *name, void *ctx) {
  return std::string(name) == "path" ? static_cast<Upload *>(ctx)->path : "";
}
int chunk(uint8_t *out, size_t max, void *ctx) {
  auto &upload = *static_cast<Upload *>(ctx);
  if (upload.offset >= upload.stop_at) return -1;
  const size_t count = std::min({max, size_t(117), upload.bytes.size() - upload.offset, upload.stop_at - upload.offset});
  std::memcpy(out, upload.bytes.data() + upload.offset, count);
  upload.offset += count;
  if (upload.remove_track && upload.offset == upload.bytes.size()) {
    std::lock_guard lock(g_runtime.mutex);
    g_runtime.playlist.clear();
  }
  return int(count);
}
DeviceOperationResult send(Upload &upload, bool streamed = false) {
  upload.offset = 0;
  DeviceBulkRequest request;
  request.context = &upload;
  request.arg = argument;
  request.content_length = upload.bytes.size();
  if (streamed) request.read_chunk = chunk;
  else { request.body_data = upload.bytes.data(); request.body_size = upload.bytes.size(); }
  JsonDocument response;
  return handle_cover_upload(request, response.to<JsonObject>());
}

std::vector<uint8_t> image() {
  std::vector<uint8_t> data(cover::kBmpBytes);
  auto le = [&](size_t pos, uint32_t v) { for (int i=0;i<4;++i) data[pos+i]=uint8_t(v>>(i*8)); };
  data[0]='B';data[1]='M';le(2,data.size());le(10,54);le(14,40);le(18,240);le(22,240);
  data[26]=1;data[28]=24;data[56]=255;
  return data;
}
bool sent = false;
bool output(int status, const char *type, const uint8_t *bytes, size_t size, void *) {
  assert(status == 200 && std::string(type) == "image/bmp");
  assert(cover::valid_bmp(bytes,size)); sent = true; return true;
}
int main() {
  const auto root=std::filesystem::temp_directory_path()/("speaker-cover-api-"+std::to_string(std::random_device{}()));
  std::filesystem::create_directory(root);
  const auto track=(root/"song.MP3").string();
  g_runtime.playlist={{"song.MP3",track}};g_runtime.current_index=0;
  Upload upload{track,image()};
  assert(send(upload).ok);
  assert(g_runtime.playlist[0].has_cover && g_runtime.cover_image);
  const auto old=g_runtime.cover_image;
  upload.bytes[0]='X';assert(send(upload).error=="cover_image_invalid");upload.bytes[0]='B';
  assert(g_runtime.cover_image==old);
  upload.stop_at=211;assert(send(upload,true).error=="cover_read_failed");upload.stop_at=SIZE_MAX;
  assert(g_runtime.cover_image==old);
  sd_ready=false;assert(send(upload).error=="sdcard_not_ready");sd_ready=true;
  g_player_operation_mutex.lock();assert(send(upload).error=="speaker_busy");g_player_operation_mutex.unlock();
  upload.bytes[56]=60;assert(send(upload,true).ok);assert(g_runtime.cover_image!=old);
  assert(old->pixels.get()[239*240]==0xf800);
  DeviceMediaWriter writer; writer.send_binary=output;
  assert(handle_cover_image({track,0},writer).ok && sent);
  assert(handle_cover_image({"/../../private.mp3",0},writer).error=="track_not_found");
  JsonDocument request,response;request["path"]=track;
  assert(handle_cover_remove(request.as<JsonVariantConst>(),response.to<JsonObject>()).ok);
  assert(!g_runtime.cover_image && !g_runtime.playlist[0].has_cover);
  assert(handle_cover_image({track,0},writer).error=="cover_not_found");
  upload.remove_track=true;
  assert(send(upload,true).error=="track_not_found");
  assert(!cover::exists(track));
  std::filesystem::remove_all(root);
  std::cout << "Cover API: streaming, cancel/short read, malformed data, SD faults, busy, replacement lifetime, deletion and stale association passed\n";
}
