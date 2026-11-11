#include "smart_speaker_cover.h"

#include <cerrno>
#include <cstdio>
#include <sys/stat.h>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace smart_speaker::cover {
namespace {
void *allocate(size_t size) {
#ifdef ESP_PLATFORM
  return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return std::malloc(size);
#endif
}
uint16_t le16(const uint8_t *p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t le32(const uint8_t *p) { return uint32_t(le16(p)) | (uint32_t(le16(p + 2)) << 16); }
std::string readable(const std::string &path) {
  struct stat info = {};
  if (stat(path.c_str(), &info) == 0) return path;
  if (stat((path + ".bak").c_str(), &info) == 0) return path + ".bak";
  return path;
}
}

Bmp allocate_bmp() { return Bmp(static_cast<uint8_t *>(allocate(kBmpBytes))); }

std::shared_ptr<const Image> decode(const uint8_t *data, size_t size) {
  if (!valid_bmp(data, size)) return {};
  auto image = std::make_shared<Image>();
  image->pixels.reset(static_cast<uint16_t *>(allocate(kSide * kSide * 2)));
  if (!image->pixels) return {};
  for (size_t y = 0; y < kSide; ++y) {
    const auto *row = data + 54 + (kSide - 1 - y) * kSide * 3;
    for (size_t x = 0; x < kSide; ++x) {
      const auto *p = row + x * 3;
      image->pixels.get()[y * kSide + x] = uint16_t((p[2] >> 3) << 11) |
          uint16_t((p[1] >> 2) << 5) | uint16_t(p[0] >> 3);
    }
  }
  return image;
}

std::shared_ptr<const Image> load(const std::string &track_path, std::string &error) {
  auto bmp = allocate_bmp();
  if (!bmp) { error = "cover_memory_unavailable"; return {}; }
  if (!read(track_path, bmp.get(), kBmpBytes, error)) return {};
  auto image = decode(bmp.get(), kBmpBytes);
  if (!image) error = "cover_memory_unavailable";
  return image;
}

bool valid_bmp(const uint8_t *data, size_t size) {
  return data && size == kBmpBytes && data[0] == 'B' && data[1] == 'M' &&
      le32(data + 2) == size && le32(data + 6) == 0 && le32(data + 10) == 54 &&
      le32(data + 14) == 40 && le32(data + 18) == kSide && le32(data + 22) == kSide &&
      le16(data + 26) == 1 && le16(data + 28) == 24 && le32(data + 30) == 0 &&
      (le32(data + 34) == 0 || le32(data + 34) == size - 54) &&
      le32(data + 46) == 0 && le32(data + 50) == 0;
}

std::string path_for_track(const std::string &track_path) {
  if (track_path.size() < 5 || track_path[track_path.size() - 4] != '.' ||
      (track_path[track_path.size() - 3] != 'm' && track_path[track_path.size() - 3] != 'M') ||
      (track_path[track_path.size() - 2] != 'p' && track_path[track_path.size() - 2] != 'P') ||
      track_path.back() != '3')
    return {};
  for (const char c : track_path) if (c == '\n' || c == '\r') return {};
  return track_path + ".cover.bmp";
}

bool exists(const std::string &track_path) {
  const auto path = path_for_track(track_path);
  struct stat info = {};
  return !path.empty() && stat(readable(path).c_str(), &info) == 0 && S_ISREG(info.st_mode) &&
      static_cast<size_t>(info.st_size) == kBmpBytes;
}

bool save(const std::string &track_path, const uint8_t *data, size_t size,
          std::string &error) {
  const auto path = path_for_track(track_path);
  if (path.empty()) { error = "cover_track_invalid"; return false; }
  if (!valid_bmp(data, size)) { error = "cover_image_invalid"; return false; }
  const auto temporary = path + ".upload";
  FILE *file = fopen(temporary.c_str(), "wb");
  if (!file) { error = "cover_write_failed"; return false; }
  bool ok = fwrite(data, 1, size, file) == size;
  if (fflush(file) != 0) ok = false;
  if (fclose(file) != 0) ok = false;
  const auto backup = path + ".bak";
  // FAT rename does not replace an existing target. Retain a recoverable old
  // version across the rename window; readers use it if power was interrupted.
  struct stat info = {};
  const bool had_active = stat(path.c_str(), &info) == 0;
  if (ok && had_active) {
    if (std::remove(backup.c_str()) != 0 && errno != ENOENT) ok = false;
    if (ok) ok = rename(path.c_str(), backup.c_str()) == 0;
  }
  if (ok) ok = rename(temporary.c_str(), path.c_str()) == 0;
  if (!ok && had_active) {
    if (stat(path.c_str(), &info) != 0) (void)rename(backup.c_str(), path.c_str());
  }
  if (!ok) { std::remove(temporary.c_str()); error = "cover_write_failed"; return false; }
  (void)std::remove(backup.c_str());
  error.clear();
  return true;
}

bool read(const std::string &track_path, uint8_t *data, size_t size,
          std::string &error) {
  const auto path = path_for_track(track_path);
  if (path.empty()) { error = "cover_track_invalid"; return false; }
  if (!data || size != kBmpBytes) { error = "cover_buffer_invalid"; return false; }
  FILE *file = fopen(readable(path).c_str(), "rb");
  if (!file) { error = "cover_not_found"; return false; }
  const size_t got = fread(data, 1, size, file);
  const int extra = fgetc(file);
  const bool ok = got == size && extra == EOF && !ferror(file);
  fclose(file);
  if (!ok || !valid_bmp(data, size)) {
    error = "cover_read_failed";
    return false;
  }
  error.clear();
  return true;
}

bool remove(const std::string &track_path, std::string &error) {
  const auto path = path_for_track(track_path);
  if (path.empty()) { error = "cover_track_invalid"; return false; }
  for (const char *suffix : {".upload", ".bak", ""}) {
    if (std::remove((path + suffix).c_str()) != 0 && errno != ENOENT) { error = "cover_remove_failed"; return false; }
  }
  error.clear();
  return true;
}
} // namespace smart_speaker::cover
