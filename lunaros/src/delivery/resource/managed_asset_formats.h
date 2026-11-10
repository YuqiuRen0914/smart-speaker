#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace lunaredu::lunaros::internal {
using AssetReadAt = bool (*)(size_t offset, uint8_t *data, size_t size, void *context);
inline uint32_t asset_u32(const uint8_t *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
// Bounded-memory validation for installers. Model consumers additionally check
// the loaded buffer; downloading never needs a second complete PSRAM copy.
inline bool managed_asset_file_valid(std::string_view id, size_t size, AssetReadAt read, void *context) {
  uint8_t header[40] = {};
  auto get = [&](size_t offset, size_t bytes) {
    return offset <= size && bytes <= size - offset && read(offset, header, bytes, context);
  };
  if (!get(0, 16)) return false;
  if (id == "fonts") {
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
  if (id == "model") {
    const size_t models = asset_u32(header);
    if (!models || models > 16) return false;
    size_t cursor = 4;
    for (size_t i = 0; i < models; ++i) {
      if (!get(cursor, 36) || !std::memchr(header, 0, 32)) return false;
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
  auto edl = [&](size_t offset) {
    if (!get(offset, 20) || std::memcmp(header, "EDL2", 4) != 0 || asset_u32(header + 4) != 0) return false;
    const size_t bytes = asset_u32(header + 8);
    return offset % 16 == 0 && bytes >= 8 && bytes <= size - offset - 16 && asset_u32(header + 16) < bytes;
  };
  if (std::memcmp(header, "EDL2", 4) == 0) return edl(0);
  if (std::memcmp(header, "PDL2", 4) != 0) return false;
  const size_t count = asset_u32(header + 4);
  if (!count || count > 8 || count > (size - 8) / 12) return false;
  for (size_t i = 0; i < count; ++i) {
    if (!get(8 + i * 12, 12)) return false;
    const size_t offset = asset_u32(header), name = asset_u32(header + 4), length = asset_u32(header + 8);
    if (offset < 8 + count * 12 || name >= size || !length || length > 128 || length > size - name || !edl(offset)) return false;
  }
  return true;
}
// Buffer and streamed-file consumers share the same format rules. This adapter
// supplies bounded reads without duplicating font/model parsing.
inline bool managed_asset_format_valid(std::string_view id, const uint8_t *data, size_t size) {
  if (data == nullptr) return false;
  struct Buffer { const uint8_t *data; size_t size; } buffer{data, size};
  auto read = [](size_t offset, uint8_t *output, size_t bytes, void *opaque) {
    const auto &source = *static_cast<const Buffer *>(opaque);
    if (offset > source.size || bytes > source.size - offset) return false;
    std::memcpy(output, source.data + offset, bytes);
    return true;
  };
  return managed_asset_file_valid(id, size, read, &buffer);
}
} // namespace lunaredu::lunaros::internal
