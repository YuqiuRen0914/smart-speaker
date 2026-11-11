#include "pet_frame_api.h"
#include "product/desktop_resources.h"
#include "lunaredu_lunaros/persistence/storage.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>

using namespace smart_speaker;
using namespace smart_speaker::product;
namespace {
std::string root;
struct Upload {
  std::string clip = "happy", index = "0";
  std::vector<uint8_t> bytes;
  size_t offset = 0, stop_at = SIZE_MAX;
  int mode = 0; // 0: short read; 1: zero; 2: invalid over-read
};
std::string arg(const char *key, void *context) {
  const auto &upload = *static_cast<Upload *>(context);
  return std::string(key) == "clip" ? upload.clip : upload.index;
}
int chunk(uint8_t *out, size_t max_size, void *context) {
  auto &upload = *static_cast<Upload *>(context);
  if (upload.mode == 1) return 0;
  if (upload.mode == 2) return static_cast<int>(max_size + 1);
  if (upload.offset >= upload.stop_at) return -1;
  const size_t count = std::min({max_size, size_t(317), upload.bytes.size() - upload.offset,
                                 upload.stop_at - upload.offset});
  std::memcpy(out, upload.bytes.data() + upload.offset, count);
  upload.offset += count;
  return static_cast<int>(count);
}
DeviceOperationResult send(Upload &upload, bool streamed = true) {
  upload.offset = 0;
  DeviceBulkRequest request;
  request.context = &upload;
  request.arg = arg;
  request.content_length = upload.bytes.size();
  if (streamed) request.read_chunk = chunk;
  else { request.body_data = upload.bytes.data(); request.body_size = upload.bytes.size(); }
  JsonDocument response;
  return handle_pet_frame_upload(request, response.to<JsonObject>());
}
std::vector<uint8_t> read(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
}

namespace lunaredu::device {
bool tfcard_resolve_path(const char *path, std::string &resolved) {
  resolved = root + path; return true;
}
}
namespace lunaredu::lunaros {
bool storage_ready() { return true; }
bool storage_path_exists(const char *) { return false; }
bool storage_write_text_file(const char *, const char *) { return true; }
bool storage_stream_file(const char *, StorageFileChunkWriter, void *, std::string *) { return false; }
}
int main() {
  const auto temp = std::filesystem::temp_directory_path() /
      ("speaker-pet-frame-api-" + std::to_string(std::random_device{}()));
  std::filesystem::create_directories(temp / "characters/dafeiyu/happy");
  root = temp.string();
  const auto target = temp / "characters/dafeiyu/happy/00.rgb565";
  std::vector<uint8_t> original(kPetFramePixels * 2, 0x2a);
  { std::ofstream file(target, std::ios::binary); file.write(reinterpret_cast<const char *>(original.data()), original.size()); }
  Upload upload;
  upload.bytes.assign(kPetFramePixels * 2, 0x7b);
  upload.stop_at = 4096;
  assert(send(upload).error == "pet_frame_read_failed");
  assert(read(target) == original);
  upload.stop_at = SIZE_MAX; upload.mode = 1;
  assert(send(upload).error == "pet_frame_read_failed");
  assert(read(target) == original);
  upload.mode = 2;
  assert(send(upload).error == "pet_frame_read_failed");
  assert(read(target) == original);
  upload.mode = 0;
  auto result = send(upload);
  assert(result.ok && read(target) == upload.bytes);

  upload.clip = "../happy";
  assert(send(upload).error == "invalid_pet_frame");
  upload.clip = "happy"; upload.index = "99";
  assert(send(upload).error == "invalid_pet_frame");
  upload.index = "0";
  upload.bytes.resize(upload.bytes.size() - 1);
  assert(send(upload).error == "pet_frame_size_invalid");
  std::filesystem::remove_all(temp);
}
