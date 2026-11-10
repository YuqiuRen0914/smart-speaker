#pragma once
#include <algorithm>
#include <array>
#include <cstring>
#include "lunaredu_remote_control_protocol.h"
#include "lunaros_crc32.h"

namespace lunaredu::remote_control {
// Presentation payload models. LP framing is owned by the shared codec.
constexpr uint8_t kLinkHasPresentation = 1;
constexpr size_t kIconChunkBytes = 192, kMaxIconBytes = 512;
constexpr uint16_t kRequestProfile = 0xffff;
struct DeviceProfileFrame {
  uint8_t channel = 1, icon_format = 1; // row-major 1 bpp, MSB first, byte-aligned rows
  uint64_t binding_nonce = 0;
  uint32_t icon_revision = 1, icon_crc32 = 0;
  uint16_t icon_bytes = 0, foreground_rgb565 = 0x07ff;
  uint8_t icon_width = 0, icon_height = 0;
  char manufacturer[16] = {}, product[24] = {}, display_name[24] = {};
};
struct AssetRequestFrame {
  uint16_t offset = kRequestProfile;
  uint64_t binding_nonce = 0;
  uint32_t icon_revision = 0;
};
struct IconChunkFrame {
  uint16_t offset = 0;
  uint64_t binding_nonce = 0;
  uint32_t icon_revision = 0;
  uint16_t length = 0;
  uint8_t data[kIconChunkBytes] = {};
};
template<size_t N> bool presentation_text_valid(const char (&text)[N]) {
  if (!text[0]) return false;
  for (size_t i = 0; i < N; ++i) {
    if (!text[i]) return true;
    if (static_cast<unsigned char>(text[i]) < 32 || static_cast<unsigned char>(text[i]) > 126) return false;
  }
  return false;
}
inline bool valid_device_profile(const DeviceProfileFrame &f) {
  return f.binding_nonce && f.channel >= 1 && f.channel <= 14 && f.icon_format == 1 && f.icon_revision &&
      f.icon_width > 0 && f.icon_width <= 64 && f.icon_height > 0 && f.icon_height <= 64 &&
      f.icon_bytes == ((f.icon_width + 7) / 8) * f.icon_height && f.icon_bytes <= kMaxIconBytes &&
      presentation_text_valid(f.manufacturer) && presentation_text_valid(f.product) && presentation_text_valid(f.display_name);
}
inline bool valid_asset_request(const AssetRequestFrame &f) {
  return f.binding_nonce && (f.offset == kRequestProfile ||
      (f.icon_revision && f.offset < kMaxIconBytes && f.offset % kIconChunkBytes == 0));
}
inline bool same_device_profile(const DeviceProfileFrame &a, const DeviceProfileFrame &b) {
  return a.channel == b.channel && a.icon_format == b.icon_format &&
      a.binding_nonce == b.binding_nonce && a.icon_revision == b.icon_revision &&
      a.icon_crc32 == b.icon_crc32 && a.icon_bytes == b.icon_bytes &&
      a.foreground_rgb565 == b.foreground_rgb565 && a.icon_width == b.icon_width &&
      a.icon_height == b.icon_height &&
      std::memcmp(a.manufacturer,b.manufacturer,sizeof(a.manufacturer)) == 0 &&
      std::memcmp(a.product,b.product,sizeof(a.product)) == 0 &&
      std::memcmp(a.display_name,b.display_name,sizeof(a.display_name)) == 0;
}
// Volatile cache only: no NVS/filesystem dependency and no product artwork.
class RemotePresentation {
 public:
  void reset() { has_profile_ = ready_ = false; received_ = 0; profile_ = {}; pixels_.fill(0); ++generation_; }
  bool accept_profile(const DeviceProfileFrame &f, uint64_t nonce) {
    if (!valid_device_profile(f) || f.binding_nonce != nonce) return false;
    if (has_profile_ && same_device_profile(profile_, f)) return true;
    reset(); profile_ = f; has_profile_ = true; return true;
  }
  bool accept_chunk(const IconChunkFrame &f, uint64_t nonce) {
    if (!has_profile_ || ready_ || f.binding_nonce != nonce || nonce != profile_.binding_nonce ||
        f.icon_revision != profile_.icon_revision || f.offset >= profile_.icon_bytes ||
        f.offset % kIconChunkBytes != 0 || f.length != std::min<size_t>(kIconChunkBytes, profile_.icon_bytes - f.offset)) return false;
    std::memcpy(pixels_.data() + f.offset, f.data, f.length);
    received_ |= 1U << (f.offset / kIconChunkBytes);
    const unsigned chunks = (profile_.icon_bytes + kIconChunkBytes - 1) / kIconChunkBytes;
    if (received_ == ((1U << chunks) - 1)) {
      if (::lunaredu::lunaros::digest::crc32(pixels_.data(), profile_.icon_bytes) != profile_.icon_crc32) {
        received_ = 0; pixels_.fill(0); return false;
      }
      ready_ = true; ++generation_;
    }
    return true;
  }
  bool next_request(uint64_t nonce, AssetRequestFrame &request) const {
    if (!nonce || ready_) return false;
    request = {}; request.binding_nonce = nonce;
    if (!has_profile_) return true;
    request.icon_revision = profile_.icon_revision;
    for (size_t offset = 0; offset < profile_.icon_bytes; offset += kIconChunkBytes) {
      if (!(received_ & (1U << (offset / kIconChunkBytes)))) {
        request.offset = static_cast<uint16_t>(offset); return true;
      }
    }
    return false;
  }
  bool has_profile() const { return has_profile_; }
  bool ready() const { return ready_; }
  uint32_t generation() const { return generation_; }
  const DeviceProfileFrame &profile() const { return profile_; }
  const std::array<uint8_t, kMaxIconBytes> &pixels() const { return pixels_; }
 private:
  DeviceProfileFrame profile_;
  std::array<uint8_t, kMaxIconBytes> pixels_{};
  bool has_profile_ = false, ready_ = false;
  uint8_t received_ = 0;
  uint32_t generation_ = 0;
};
} // namespace lunaredu::remote_control
