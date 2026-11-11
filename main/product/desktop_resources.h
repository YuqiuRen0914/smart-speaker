#pragma once

#include "product_state.h"
#include "pet_animation_pack.h"
#include <cstdlib>
#include <memory>

namespace smart_speaker::product {

// Images are validated/converted on the I/O owner and immutable afterwards.
// The display holds a shared reference until LVGL releases the previous source.
constexpr size_t kWallpaperSide = 240;
constexpr size_t kWallpaperBmpBytes = 54 + kWallpaperSide * kWallpaperSide * 3;
void *allocate_wallpaper_bytes(size_t bytes);
struct FreeWallpaperBuffer { void operator()(void *p) const { std::free(p); } };
// Bounded, checked PSRAM allocations; an exhausted heap returns an error to
// the caller instead of terminating through std::vector's allocation path.
template <typename T> class WallpaperBuffer {
 public:
  bool resize(size_t count) {
    if (count > kWallpaperBmpBytes / sizeof(T)) return false;
    auto *next = static_cast<T *>(allocate_wallpaper_bytes(count * sizeof(T)));
    if (!next) return false;
    data_.reset(next); count_ = count; return true;
  }
  void clear() { data_.reset(); count_ = 0; }
  T *data() { return data_.get(); }
  const T *data() const { return data_.get(); }
  size_t size() const { return count_; }
  T &operator[](size_t index) { return data_.get()[index]; }
  const T &operator[](size_t index) const { return data_.get()[index]; }
 private:
  std::unique_ptr<T, FreeWallpaperBuffer> data_;
  size_t count_ = 0;
};
using WallpaperPixels = WallpaperBuffer<uint16_t>;
using WallpaperBmp = WallpaperBuffer<uint8_t>;
constexpr size_t kPetFrameSide = 160;
constexpr size_t kPetFramePixels = kPetFrameSide * kPetFrameSide;
enum class PetFrame : uint8_t {
  Idle, Happy, Angry, Think, Talk,
  HeadPat0, HeadPat1, HeadPat2, HeadPat3, HeadPat4, HeadPat5,
  Poke0, Poke1, Poke2, Poke3,
  Tail0, Tail1, Tail2, Tail3,
  Count,
};
struct DesktopResourcesSnapshot {
  std::string selected;
  std::string error;
  std::shared_ptr<const WallpaperPixels> pixels;
  std::shared_ptr<const WallpaperPixels> pet_frame;
  PetFrame requested_pet_frame = PetFrame::Idle;
  PetFrame loaded_pet_frame = PetFrame::Count;
  std::string pet_frame_error;
  bool pet_frame_pending = false;
};

struct PetAnimationFrame {
  std::shared_ptr<const WallpaperPixels> pixels;
  PetClip clip = PetClip::Count;
  uint16_t index = 0;
  uint16_t frame_count = 0;
  bool unavailable = false;
};

bool decode_wallpaper_bmp(const uint8_t *data, size_t size, WallpaperPixels &pixels);
bool wallpaper_name_valid(const std::string &name);
DesktopResourcesSnapshot desktop_resources_snapshot();
// Startup runs before the display. SD reads and mutations run on network I/O.
bool desktop_resources_load();
// Display posts a coalescing frame request; the network/storage owner reads
// the fixed-size file and publishes an immutable PSRAM snapshot.
void pet_frame_request(PetFrame frame);
void pet_frame_retry(PetFrame frame);
void pet_frame_poll_request(uint32_t now_ms);
// A single latest-frame mailbox; Count cancels and releases the stream.
void pet_animation_request(PetClip clip, unsigned index = 0);
PetAnimationFrame pet_animation_snapshot();
// Last SD validation, retained while the menu has suspended frame streaming.
bool pet_animation_catalog_ready();
void pet_animation_poll(uint32_t now_ms);
bool pet_frame_parse(const std::string &clip, unsigned index, PetFrame &frame);
bool pet_frame_upload(PetFrame frame, const uint8_t *bytes, size_t size,
                     std::string &error);
bool wallpaper_list(std::vector<std::string> &names, std::string &error);
bool wallpaper_upload(const std::string &name, const WallpaperBmp &bmp, std::string &error);
bool wallpaper_select(const std::string &name, std::string &error);
bool wallpaper_read(const std::string &name, WallpaperBmp &bmp, std::string &error);
bool wallpaper_remove(const std::string &name, std::string &error);

} // namespace smart_speaker::product
