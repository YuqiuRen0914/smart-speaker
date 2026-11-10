#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace lunaredu::lunaros::internal {
inline constexpr size_t kAssetDecodedMaximum = 8 * 1024 * 1024;
inline bool compressed_asset(const uint8_t *data, size_t bytes) {
  return data && bytes >= 4 && std::memcmp(data, "LRZ1", 4) == 0;
}
inline bool compressed_asset_header(const uint8_t *data, size_t bytes,
                                    size_t &decoded) {
  decoded = 0;
  if (!compressed_asset(data, bytes) || bytes < 18) return false;
  auto u32 = [](const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
  };
  const size_t count = u32(data + 8);
  if (u32(data + 4) != 1 || count < 16 || count > kAssetDecodedMaximum ||
      u32(data + 12) != bytes - 16) return false;
  decoded = count;
  return true;
}
// Drop the single idle S3/S31 ROM workspace; a current decode retires on return.
void retire_managed_asset_decoder();
// Takes ownership of the verified transport buffer only on successful decode.
// Raw publications retain their buffer and byte count.
bool decode_managed_asset(uint8_t *&data, size_t &bytes, std::string &error);
}
