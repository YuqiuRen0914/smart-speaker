#include "desktop_resources.h"
#include "lunaredu_device_sdcard_tfcard_store.h"
#include "lunaredu_lunaros/persistence/storage.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <mutex>
#include <sys/stat.h>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace smart_speaker::product {
void *allocate_wallpaper_bytes(size_t bytes) {
#ifdef ESP_PLATFORM
  return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return std::malloc(bytes);
#endif
}
namespace {
constexpr const char *kConfig = "/user/product_desktop.json";
constexpr size_t kMaxWallpapers = 32;
std::mutex g_io_mutex, g_snapshot_mutex;
DesktopResourcesSnapshot g_snapshot;
std::atomic<int> g_requested_pet_frame{-1};
std::atomic<uint32_t> g_pet_frame_retry_generation{0};
int g_tracked_pet_frame = -1;
uint32_t g_seen_pet_frame_retry_generation = 0;
uint32_t g_pet_frame_retry_at_ms = 0;
uint8_t g_pet_frame_attempts = 0;
int g_processed_pet_frame = -1;
constexpr uint8_t kPetFrameMaxAttempts = 3;
std::atomic<int> g_animation_request{-1};
std::atomic<bool> g_animation_catalog_ready{false};
PetAnimationFrame g_animation;
std::array<std::shared_ptr<WallpaperPixels>, 2> g_animation_buffers;
int g_animation_loaded = -1;
bool g_animation_active = false;
bool g_animation_retrying = false;
uint32_t g_animation_retry_ms = 0;
struct PetFramePath { const char *clip; uint8_t index; };
constexpr PetFramePath kPetFramePaths[] = {
    {"idle", 0}, {"happy", 0}, {"angry", 0}, {"think", 0}, {"talk", 0},
    {"head_pat", 0}, {"head_pat", 1}, {"head_pat", 2},
    {"head_pat", 3}, {"head_pat", 4}, {"head_pat", 5},
    {"poke_react", 0}, {"poke_react", 1}, {"poke_react", 2}, {"poke_react", 3},
    {"tail_react", 0}, {"tail_react", 1}, {"tail_react", 2}, {"tail_react", 3},
};
static_assert(sizeof(kPetFramePaths) / sizeof(kPetFramePaths[0]) ==
              static_cast<size_t>(PetFrame::Count));

uint16_t le16(const uint8_t *p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t le32(const uint8_t *p) { return uint32_t(le16(p)) | (uint32_t(le16(p + 2)) << 16); }

void record_error(const std::string &error) {
  std::lock_guard lock(g_snapshot_mutex);
  g_snapshot.error = error;
}

bool directory(std::string &path, std::string &error) {
  if (!lunaredu::device::tfcard_resolve_path("/wallpapers", path)) {
    error = "sdcard_not_ready"; return false;
  }
  if (mkdir(path.c_str(), 0775) != 0 && errno != EEXIST) {
    error = "wallpaper_directory_unavailable"; return false;
  }
  return true;
}

bool read_image(const std::string &name, WallpaperBmp &bmp, std::string &error) {
  if (!wallpaper_name_valid(name)) { error = "invalid_wallpaper_name"; return false; }
  std::string base;
  if (!directory(base, error)) return false;
  const auto path = base + "/" + name;
  FILE *file = fopen(path.c_str(), "rb");
  if (!file) { error = "wallpaper_not_found"; return false; }
  if (!bmp.resize(kWallpaperBmpBytes)) { fclose(file); error = "wallpaper_memory_unavailable"; return false; }
  const size_t got = fread(bmp.data(), 1, bmp.size(), file);
  const int extra = fgetc(file);
  const bool ok = got == bmp.size() && extra == EOF && !ferror(file);
  fclose(file);
  if (!ok) { bmp.clear(); error = "wallpaper_read_failed"; }
  return ok;
}

bool save_config(const std::string &selected, std::string &error) {
  JsonDocument config;
  config["schema"] = 1;
  config["mode"] = "wallpaper";
  config["wallpaper"] = selected;
  std::string serialized;
  serializeJson(config, serialized);
  if (!lunaredu::lunaros::storage_write_text_file(kConfig, serialized.c_str())) {
    error = "desktop_save_failed"; record_error(error); return false;
  }
  return true;
}

bool list_images(std::vector<std::string> &names, std::string &error) {
  names.clear();
  std::string base;
  if (!directory(base, error)) return false;
  DIR *dir = opendir(base.c_str());
  if (!dir) { error = "wallpaper_list_failed"; return false; }
  while (auto *entry = readdir(dir)) {
    if (!wallpaper_name_valid(entry->d_name)) continue;
    names.emplace_back(entry->d_name);
    if (names.size() > kMaxWallpapers) {
      closedir(dir); names.clear(); error = "wallpaper_limit_exceeded"; return false;
    }
  }
  closedir(dir);
  std::sort(names.begin(), names.end());
  return true;
}

void remove_interrupted_uploads(const std::string &base) {
  DIR *dir = opendir(base.c_str());
  if (!dir) return;
  while (auto *entry = readdir(dir)) {
    const std::string file = entry->d_name;
    constexpr size_t suffix = sizeof(".bmp.upload") - 1;
    if (file.size() <= suffix || file.compare(file.size() - suffix, suffix, ".bmp.upload") != 0)
      continue;
    const std::string canonical = file.substr(0, file.size() - sizeof(".upload") + 1);
    if (wallpaper_name_valid(canonical)) (void)remove((base + "/" + file).c_str());
  }
  closedir(dir);
}
} // namespace

bool wallpaper_name_valid(const std::string &name) {
  if (name.size() < 5 || name.size() > 48 || name.substr(name.size() - 4) != ".bmp") return false;
  for (size_t i = 0; i < name.size() - 4; ++i) {
    const char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  }
  return true;
}

bool decode_wallpaper_bmp(const uint8_t *data, size_t size, WallpaperPixels &pixels) {
  // One canonical, uncompressed 24-bit Windows BMP. A fixed stride/offset
  // makes malformed dimensions and palette/compression bombs impossible.
  if (!data || size != kWallpaperBmpBytes || data[0] != 'B' || data[1] != 'M' ||
      le32(data + 2) != size || le32(data + 6) != 0 || le32(data + 10) != 54 ||
      le32(data + 14) != 40 || le32(data + 18) != kWallpaperSide ||
      le32(data + 22) != kWallpaperSide || le16(data + 26) != 1 ||
      le16(data + 28) != 24 || le32(data + 30) != 0 ||
      (le32(data + 34) != 0 && le32(data + 34) != size - 54) ||
      le32(data + 46) != 0 || le32(data + 50) != 0) return false;
  if (!pixels.resize(kWallpaperSide * kWallpaperSide)) return false;
  for (size_t y = 0; y < kWallpaperSide; ++y) {
    const uint8_t *row = data + 54 + (kWallpaperSide - 1 - y) * kWallpaperSide * 3;
    for (size_t x = 0; x < kWallpaperSide; ++x) {
      const uint8_t *bgr = row + x * 3;
      pixels[y * kWallpaperSide + x] = uint16_t((bgr[2] >> 3) << 11) |
          uint16_t((bgr[1] >> 2) << 5) | uint16_t(bgr[0] >> 3);
    }
  }
  return true;
}

DesktopResourcesSnapshot desktop_resources_snapshot() {
  std::lock_guard lock(g_snapshot_mutex);
  auto result = g_snapshot;
  return result;
}

bool desktop_resources_load() {
  std::lock_guard io(g_io_mutex);
  if (!lunaredu::lunaros::storage_ready()) { record_error("desktop_storage_unavailable"); return false; }
  if (!lunaredu::lunaros::storage_path_exists(kConfig)) return true;
  std::string text;
  // Use bounded streaming even for damaged persistent records.
  if (!lunaredu::lunaros::storage_stream_file(kConfig,
      [](const uint8_t *data, size_t size, void *context) {
        auto &body = *static_cast<std::string *>(context);
        if (size > 512 - body.size()) return false;
        body.append(reinterpret_cast<const char *>(data), size); return true;
      }, &text)) { record_error("desktop_read_failed"); return false; }
  JsonDocument config;
  if (deserializeJson(config, text, DeserializationOption::NestingLimit(2)) || config["schema"] != 1 ||
      !config["mode"].is<const char *>() || !config["wallpaper"].is<const char *>()) {
    record_error("desktop_record_invalid"); return false;
  }
  // Schema 1 stored a mode. Retain its wallpaper for all legacy modes.
  const std::string mode = config["mode"].as<std::string>();
  const std::string name = config["wallpaper"].as<std::string>();
  if ((mode != "pet" && mode != "clock" && mode != "wallpaper") ||
      (!name.empty() && !wallpaper_name_valid(name))) { record_error("desktop_record_invalid"); return false; }
  std::shared_ptr<WallpaperPixels> image;
  std::string error;
  if (!name.empty()) {
    WallpaperBmp bmp;
    image = std::make_shared<WallpaperPixels>();
    if (!read_image(name, bmp, error) || !decode_wallpaper_bmp(bmp.data(), bmp.size(), *image)) {
      if (error.empty()) error = "wallpaper_invalid";
      image.reset();
    }
  }
  std::string base;
  std::string directory_error;
  if (directory(base, directory_error)) remove_interrupted_uploads(base);
  if (error.empty()) error = directory_error;
  {
    std::lock_guard lock(g_snapshot_mutex);
    g_snapshot.selected = name;
    g_snapshot.pixels = std::move(image);
    g_snapshot.error = error;
  }
  return error.empty();
}

void pet_frame_request(PetFrame frame) {
  if (frame >= PetFrame::Count) return;
  const int requested = static_cast<int>(frame);
  const int previous = g_requested_pet_frame.exchange(requested, std::memory_order_acq_rel);
  if (previous == requested) return;
  std::lock_guard lock(g_snapshot_mutex);
  g_snapshot.requested_pet_frame = frame;
  g_snapshot.pet_frame_pending = g_snapshot.loaded_pet_frame != frame;
  g_snapshot.pet_frame_error.clear();
}

void pet_frame_retry(PetFrame frame) {
  if (frame >= PetFrame::Count) return;
  g_requested_pet_frame.store(static_cast<int>(frame), std::memory_order_release);
  g_pet_frame_retry_generation.fetch_add(1, std::memory_order_acq_rel);
  std::lock_guard lock(g_snapshot_mutex);
  g_snapshot.requested_pet_frame = frame;
  g_snapshot.pet_frame_pending = true;
  g_snapshot.pet_frame_error.clear();
}

void pet_animation_request(PetClip clip, unsigned index) {
  const int id = clip < PetClip::Count && index < kPetClipMaxFrames
      ? (static_cast<int>(clip) << 8) | static_cast<int>(index) : -1;
  g_animation_request.store(id, std::memory_order_release);
}

PetAnimationFrame pet_animation_snapshot() {
  std::lock_guard lock(g_snapshot_mutex);
  return g_animation;
}

bool pet_animation_catalog_ready() {
  return g_animation_catalog_ready.load(std::memory_order_acquire);
}

void pet_animation_poll(uint32_t now_ms) {
  const int requested = g_animation_request.load(std::memory_order_acquire);
  if (requested < 0) {
    if (!g_animation_active) return;
    {
      std::lock_guard lock(g_snapshot_mutex);
      g_animation = {};
    }
    g_animation_buffers = {};
    g_animation_active = false;
    g_animation_retrying = false;
    g_animation_loaded = -1;
    return;
  }
  if (requested == g_animation_loaded ||
      (g_animation_retrying && static_cast<int32_t>(now_ms - g_animation_retry_ms) < 0)) return;
  g_animation_active = true;
  const auto clip = static_cast<PetClip>(requested >> 8);
  const auto index = static_cast<uint16_t>(requested & 255);
  auto slot = std::find_if(g_animation_buffers.begin(), g_animation_buffers.end(),
                           [](const auto &p) { return !p || p.use_count() == 1; });
  if (slot == g_animation_buffers.end()) return; // Display is still reading.

  std::unique_lock io(g_io_mutex, std::try_to_lock);
  if (!io.owns_lock()) return;
  std::string path;
  PetPackHeader header{};
  bool ok = false;
  if (lunaredu::device::tfcard_resolve_path("/characters/dsh-pet/pet-v2.idx", path)) {
    FILE *file = fopen(path.c_str(), "rb");
    if (!file && errno == ENOENT &&
        lunaredu::device::tfcard_resolve_path("/characters/dsh-pet/pet-v1.bin", path))
      file = fopen(path.c_str(), "rb");
    if (file) {
      if (read_pet_pack_header(file, header) && header.frame_offset(clip, index) >= 0) {
        if (!*slot) {
          auto pixels = std::make_shared<WallpaperPixels>();
          if (pixels->resize(kPetSpritePixels)) *slot = std::move(pixels);
        }
        if (*slot && !header.sharded()) ok = read_pet_pack_frame(file, header, clip, index, (*slot)->data());
      }
      fclose(file);
    }
    if (*slot && header.sharded() && header.frame_offset(clip, index) >= 0) {
      // FAT seek walks the cluster chain. Bound it to one second of frames
      // and close every handle on this executor before yielding the SD card.
      char relative[80];
      snprintf(relative, sizeof(relative), "/characters/dsh-pet/frames-v2/%03u/%02u.bin",
               static_cast<unsigned>(clip), static_cast<unsigned>(index / kPetFramesPerShard));
      if (lunaredu::device::tfcard_resolve_path(relative, path)) {
        if (FILE *shard = fopen(path.c_str(), "rb")) {
          ok = read_pet_shard_frame(shard, header, clip, index, (*slot)->data());
          fclose(shard);
        }
      }
    }
  }
  const int latest = g_animation_request.load(std::memory_order_acquire);
  if (latest < 0 || latest >> 8 != requested >> 8) return;
  {
    std::lock_guard lock(g_snapshot_mutex);
    g_animation = ok ? PetAnimationFrame{*slot, clip, index,
                         header.counts[static_cast<unsigned>(clip)], false}
                     : PetAnimationFrame{nullptr, PetClip::Count, 0, {}, true};
  }
  g_animation_loaded = ok ? requested : -1;
  g_animation_catalog_ready.store(ok && header.counts.back() != 0, std::memory_order_release);
  g_animation_retrying = !ok;
  if (!ok) g_animation_retry_ms = now_ms + 1000;
}

void pet_frame_poll_request(uint32_t now_ms) {
  std::lock_guard io(g_io_mutex);
  const int requested = g_requested_pet_frame.load(std::memory_order_acquire);
  if (requested < 0 || requested >= static_cast<int>(PetFrame::Count)) return;
  const uint32_t retry_generation = g_pet_frame_retry_generation.load(std::memory_order_acquire);
  if (requested != g_tracked_pet_frame || retry_generation != g_seen_pet_frame_retry_generation) {
    g_tracked_pet_frame = requested;
    g_seen_pet_frame_retry_generation = retry_generation;
    g_pet_frame_attempts = 0;
    g_pet_frame_retry_at_ms = now_ms;
    g_processed_pet_frame = -1;
  }
  if (requested == g_processed_pet_frame ||
      static_cast<int32_t>(now_ms - g_pet_frame_retry_at_ms) < 0) return;
  if (g_requested_pet_frame.load(std::memory_order_acquire) != requested) return;
  ++g_pet_frame_attempts;

  const auto spec = kPetFramePaths[requested];
  char relative[80];
  const int length = std::snprintf(relative, sizeof(relative),
      "/characters/dafeiyu/%s/%02u.rgb565", spec.clip, spec.index);
  std::string path, error;
  std::shared_ptr<WallpaperPixels> pixels;
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(relative) ||
      !lunaredu::device::tfcard_resolve_path(relative, path)) {
    error = "pet_sdcard_unavailable";
  } else {
    FILE *file = fopen(path.c_str(), "rb");
    if (!file) {
      error = "pet_frame_missing";
    } else {
      pixels = std::make_shared<WallpaperPixels>();
      if (!pixels->resize(kPetFramePixels)) {
        error = "pet_frame_memory_unavailable";
      } else {
        const size_t bytes = kPetFramePixels * sizeof(uint16_t);
        const size_t got = fread(pixels->data(), 1, bytes, file);
        const int extra = fgetc(file);
        if (got != bytes || extra != EOF || ferror(file)) {
          pixels.reset();
          error = "pet_frame_invalid_size";
        }
      }
      fclose(file);
    }
  }

  if (g_requested_pet_frame.load(std::memory_order_acquire) != requested ||
      g_pet_frame_retry_generation.load(std::memory_order_acquire) != retry_generation) return;
  {
    std::lock_guard lock(g_snapshot_mutex);
    g_snapshot.requested_pet_frame = static_cast<PetFrame>(requested);
    g_snapshot.pet_frame_pending = false;
    g_snapshot.pet_frame_error = error;
    if (pixels) {
      g_snapshot.pet_frame = std::move(pixels);
      g_snapshot.loaded_pet_frame = static_cast<PetFrame>(requested);
    }
  }
  if (pixels) {
    g_processed_pet_frame = requested;
    g_pet_frame_attempts = 0;
  } else if (g_pet_frame_attempts >= kPetFrameMaxAttempts) {
    g_processed_pet_frame = requested;
  } else {
    g_pet_frame_retry_at_ms = now_ms + 250U * g_pet_frame_attempts;
    g_processed_pet_frame = -1;
  }
}

bool pet_frame_parse(const std::string &clip, unsigned index, PetFrame &frame) {
  for (size_t i = 0; i < sizeof(kPetFramePaths) / sizeof(kPetFramePaths[0]); ++i) {
    if (clip == kPetFramePaths[i].clip && index == kPetFramePaths[i].index) {
      frame = static_cast<PetFrame>(i);
      return true;
    }
  }
  return false;
}

bool pet_frame_upload(PetFrame frame, const uint8_t *bytes, size_t size,
                      std::string &error) {
  if (frame >= PetFrame::Count || bytes == nullptr ||
      size != kPetFramePixels * sizeof(uint16_t)) {
    error = "pet_frame_invalid_size";
    return false;
  }
  std::lock_guard io(g_io_mutex);
  const auto spec = kPetFramePaths[static_cast<size_t>(frame)];
  std::string base;
  if (!lunaredu::device::tfcard_resolve_path("/characters/dafeiyu", base)) {
    error = "sdcard_not_ready";
    return false;
  }
  if (mkdir(base.c_str(), 0775) != 0 && errno != EEXIST) {
    error = "pet_frame_directory_unavailable";
    return false;
  }
  base += "/";
  base += spec.clip;
  if (mkdir(base.c_str(), 0775) != 0 && errno != EEXIST) {
    error = "pet_frame_directory_unavailable";
    return false;
  }
  char leaf[24];
  std::snprintf(leaf, sizeof(leaf), "/%02u.rgb565", spec.index);
  const std::string path = base + leaf;
  const std::string temporary = path + ".upload";
  FILE *file = fopen(temporary.c_str(), "wb");
  if (!file) { error = "pet_frame_write_failed"; return false; }
  bool ok = fwrite(bytes, 1, size, file) == size;
  if (fflush(file) != 0) ok = false;
  if (fclose(file) != 0) ok = false;
  if (ok) ok = rename(temporary.c_str(), path.c_str()) == 0;
  if (!ok) { remove(temporary.c_str()); error = "pet_frame_write_failed"; return false; }
  bool active_frame = g_requested_pet_frame.load(std::memory_order_acquire) == static_cast<int>(frame);
  {
    std::lock_guard lock(g_snapshot_mutex);
    active_frame = active_frame || g_snapshot.loaded_pet_frame == frame;
  }
  if (active_frame) {
    g_requested_pet_frame.store(static_cast<int>(frame), std::memory_order_release);
    g_pet_frame_retry_generation.fetch_add(1, std::memory_order_acq_rel);
    std::lock_guard lock(g_snapshot_mutex);
    g_snapshot.pet_frame.reset();
    g_snapshot.loaded_pet_frame = PetFrame::Count;
    g_snapshot.requested_pet_frame = frame;
    g_snapshot.pet_frame_pending = true;
    g_snapshot.pet_frame_error.clear();
  }
  error.clear();
  return true;
}

bool wallpaper_list(std::vector<std::string> &names, std::string &error) {
  std::lock_guard io(g_io_mutex); return list_images(names, error);
}
bool wallpaper_read(const std::string &name, WallpaperBmp &bmp, std::string &error) {
  std::lock_guard io(g_io_mutex); return read_image(name, bmp, error);
}
bool wallpaper_upload(const std::string &name, const WallpaperBmp &bmp, std::string &error) {
  std::lock_guard io(g_io_mutex);
  if (!wallpaper_name_valid(name)) { error = "invalid_wallpaper_name"; return false; }
  WallpaperPixels pixels;
  if (!decode_wallpaper_bmp(bmp.data(), bmp.size(), pixels)) { error = "wallpaper_invalid"; return false; }
  std::vector<std::string> names;
  if (!list_images(names, error)) return false;
  if (std::find(names.begin(), names.end(), name) != names.end()) { error = "wallpaper_exists"; return false; }
  if (names.size() >= kMaxWallpapers) { error = "wallpaper_limit_reached"; return false; }
  std::string base;
  if (!directory(base, error)) return false;
  const auto path = base + "/" + name;
  const auto temporary = path + ".upload";
  FILE *file = fopen(temporary.c_str(), "wb");
  if (!file) { error = "wallpaper_write_failed"; return false; }
  bool ok = fwrite(bmp.data(), 1, bmp.size(), file) == bmp.size();
  if (fflush(file) != 0) ok = false;
  if (fclose(file) != 0) ok = false;
  if (ok) ok = rename(temporary.c_str(), path.c_str()) == 0;
  if (!ok) { remove(temporary.c_str()); error = "wallpaper_write_failed"; }
  return ok;
}
bool wallpaper_select(const std::string &name, std::string &error) {
  std::lock_guard io(g_io_mutex);
  WallpaperBmp bmp;
  if (!read_image(name, bmp, error)) return false;
  auto image = std::make_shared<WallpaperPixels>();
  if (!decode_wallpaper_bmp(bmp.data(), bmp.size(), *image)) { error = "wallpaper_invalid"; return false; }
  if (!save_config(name, error)) return false;
  {
    std::lock_guard lock(g_snapshot_mutex);
    g_snapshot.selected = name;
    g_snapshot.pixels = std::move(image);
    g_snapshot.error.clear();
  }
  return true;
}
bool wallpaper_remove(const std::string &name, std::string &error) {
  std::lock_guard io(g_io_mutex);
  if (!wallpaper_name_valid(name)) { error = "invalid_wallpaper_name"; return false; }
  if (desktop_resources_snapshot().selected == name) { error = "wallpaper_in_use"; return false; }
  std::string base;
  if (!directory(base, error)) return false;
  if (remove((base + "/" + name).c_str()) != 0) { error = "wallpaper_remove_failed"; return false; }
  return true;
}
} // namespace smart_speaker::product
