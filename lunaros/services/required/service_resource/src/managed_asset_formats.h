#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "lunaros_resource_managed_assets.h"

namespace lunaredu::lunaros::internal {
using AssetReadAt = bool (*)(size_t offset, uint8_t *data, size_t size, void *context);
inline uint32_t asset_u32(const uint8_t *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
// Bounded-memory validation for installers. Model consumers additionally check
// the loaded buffer; downloading never needs a second complete PSRAM copy.
inline bool managed_asset_file_valid(ManagedAssetFormat format, size_t size, AssetReadAt read, void *context,
    const char *const *model_families = nullptr, size_t model_family_count = 0) {
  uint8_t header[60] = {};
  auto get = [&](size_t offset, size_t bytes) {
    return offset <= size && bytes <= size - offset && read(offset, header, bytes, context);
  };
  if (!get(0, 16)) return false;
  if (format == ManagedAssetFormat::TrueType) {
    if (std::memcmp(header, "\0\1\0\0", 4) != 0) return false;
    const size_t count = size_t(header[4]) * 256 + header[5];
    if (!count || count > 256 || count > (size - 12) / 16) return false;
    for (size_t i = 0; i < count; ++i) {
      if (!get(12 + i * 16, 16)) return false;
      const size_t offset = uint32_t(header[8]) << 24 | uint32_t(header[9]) << 16 | uint32_t(header[10]) << 8 | header[11];
      const size_t bytes = uint32_t(header[12]) << 24 | uint32_t(header[13]) << 16 | uint32_t(header[14]) << 8 | header[15];
      if (offset > size || bytes > size - offset) return false;
    }
    return true;
  }
  if (format == ManagedAssetFormat::SpeechPack) {
    const size_t models = asset_u32(header);
    if (!models || models > 16) return false;
    size_t cursor = 4;
    for (size_t i = 0; i < models; ++i) {
      if (!get(cursor, 36) || !std::memchr(header, 0, 32)) return false;
      if (model_family_count) {
        bool supported = false;
        for (size_t f = 0; f < model_family_count; ++f) {
          if (!model_families || !model_families[f]) return false;
          const size_t length = std::strlen(model_families[f]);
          if (length + 1 < 32 && std::strncmp(reinterpret_cast<const char *>(header),
                  model_families[f], length) == 0 && header[length] == '_' && header[length + 1])
            supported = true;
        }
        if (!supported) return false;
      }
      const size_t files = asset_u32(header + 32);
      cursor += 36;
      if (!files || files > 32 || files > (size - cursor) / 40) return false;
      for (size_t j = 0; j < files; ++j, cursor += 40) {
        if (!get(cursor, 40) || !std::memchr(header, 0, 32)) return false;
        const size_t offset = asset_u32(header + 32), bytes = asset_u32(header + 36);
        if (offset >= size || !bytes || bytes > size - offset) return false;
      }
    }
    return true;
  }
  if (format != ManagedAssetFormat::EspDl) return false;
  auto edl = [&](size_t offset) {
    if (!get(offset, 20) || std::memcmp(header, "EDL2", 4) != 0 || asset_u32(header + 4) != 0) return false;
    const size_t bytes = asset_u32(header + 8);
    return offset % 16 == 0 && bytes >= 8 && bytes <= size - offset - 16 && asset_u32(header + 16) < bytes;
  };
  if (std::memcmp(header, "EDL2", 4) == 0) return edl(0);
  size_t entries = 8, count = 0;
  if (std::memcmp(header, "PDL3", 4) == 0) {
    // ESP-DL's PDL3 package carries a version, size, digest and model count.
    // The managed publication's SHA256 verifies the entire file separately.
    if (!get(0, 60) || !header[4] || !std::memchr(header + 4, 0, 16) ||
        asset_u32(header + 20) != size) return false;
    entries = 60;
    count = asset_u32(header + 56);
  } else if (std::memcmp(header, "PDL2", 4) == 0) {
    count = asset_u32(header + 4);
  } else return false;
  if (!count || count > 8 || count > (size - entries) / 12) return false;
  for (size_t i = 0; i < count; ++i) {
    if (!get(entries + i * 12, 12)) return false;
    const size_t offset = asset_u32(header), name = asset_u32(header + 4), length = asset_u32(header + 8);
    if (offset < entries + count * 12 || name < entries + count * 12 || name >= size || !length || length > 128 || length > size - name || !edl(offset)) return false;
  }
  return true;
}
// Buffer and streamed-file consumers share the same format rules. This adapter
// supplies bounded reads without duplicating font/model parsing.
inline bool managed_asset_format_valid(ManagedAssetFormat format, const uint8_t *data, size_t size,
    const char *const *model_families = nullptr, size_t model_family_count = 0) {
  if (data == nullptr) return false;
  struct Buffer { const uint8_t *data; size_t size; } buffer{data, size};
  auto read = [](size_t offset, uint8_t *output, size_t bytes, void *opaque) {
    const auto &source = *static_cast<const Buffer *>(opaque);
    if (offset > source.size || bytes > source.size - offset) return false;
    std::memcpy(output, source.data + offset, bytes);
    return true;
  };
  return managed_asset_file_valid(format, size, read, &buffer, model_families, model_family_count);
}
} // namespace lunaredu::lunaros::internal
