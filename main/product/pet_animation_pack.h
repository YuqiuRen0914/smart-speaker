#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#ifdef ESP_PLATFORM
#include <sys/stat.h>
#endif

namespace smart_speaker::product {

enum class PetClip : uint8_t { Idle, Tap, Drag, Think, Talk, SleepStart, SleepLoop, SleepEnd, Count = 114 };
constexpr size_t kPetClipCount = static_cast<size_t>(PetClip::Count);
constexpr int kPetToggleSleep = static_cast<int>(PetClip::Count);
constexpr unsigned kPetActionRows = kPetClipCount + 1;
constexpr size_t kPetV1ClipCount = 5;
constexpr size_t kPetPackPrefixBytes = 12;
constexpr unsigned kPetSpriteSide = 120;
constexpr unsigned kPetSpriteFps = 12;
constexpr unsigned kPetClipMaxFrames = 240;
constexpr unsigned kPetFramesPerShard = 12;
constexpr size_t kPetSpritePixels = kPetSpriteSide * kPetSpriteSide;
constexpr size_t kPetSpriteBytes = kPetSpritePixels * sizeof(uint16_t);
constexpr size_t kPetPackHeaderBytes = kPetPackPrefixBytes + 2 * kPetV1ClipCount;
constexpr size_t kPetPackRecordBytes = 4 + kPetSpriteBytes;

inline bool pet_file_has_size(FILE *file, size_t bytes) {
#ifdef ESP_PLATFORM
  // FatFs fstat reads the open file's recorded size, without a cluster walk.
  struct stat status{};
  return fstat(fileno(file), &status) == 0 && status.st_size == static_cast<off_t>(bytes);
#else
  return fseek(file, 0, SEEK_END) == 0 && ftell(file) == static_cast<long>(bytes);
#endif
}

struct PetPackHeader {
  std::array<uint16_t, kPetClipCount> counts{};
  size_t header_bytes = kPetPackHeaderBytes;
  bool sharded() const { return header_bytes != kPetPackHeaderBytes; }
  long frame_offset(PetClip clip, unsigned index) const {
    const auto slot = static_cast<size_t>(clip);
    if (slot >= counts.size() || index >= counts[slot]) return -1;
    for (size_t i = 0; i < slot; ++i) index += counts[i];
    return static_cast<long>(header_bytes + index * kPetPackRecordBytes);
  }
};

inline bool read_pet_pack_header(FILE *file, PetPackHeader &header) {
  std::array<uint8_t, kPetPackPrefixBytes> b{};
  if (fread(b.data(), 1, b.size(), file) != b.size() ||
      b[0] != 'D' || b[1] != 'S' || b[2] != 'H' || b[3] != 'P' ||
      !((b[4] == 1 && b[5] == kPetV1ClipCount) || (b[4] == 2 && b[5] == kPetClipCount)) ||
      b[6] != kPetSpriteSide ||
      b[7] != kPetSpriteFps || b[8] != (b[4] == 2 ? kPetFramesPerShard : 0) ||
      b[9] || b[10] || b[11]) return false;
  std::array<uint8_t, 2 * kPetClipCount> counts{};
  const size_t count_bytes = 2 * b[5];
  if (fread(counts.data(), 1, count_bytes, file) != count_bytes) return false;
  PetPackHeader candidate;
  candidate.header_bytes = b.size() + count_bytes;
  size_t frames = 0;
  for (size_t i = 0; i < b[5]; ++i) {
    const auto count = uint16_t(counts[i * 2] | (counts[i * 2 + 1] << 8));
    if (count == 0 || count > kPetClipMaxFrames) return false;
    candidate.counts[i] = count;
    frames += count;
  }
  if (candidate.sharded()) {
    if (fgetc(file) != EOF || ferror(file)) return false;
  } else if (!pet_file_has_size(file, candidate.header_bytes + frames * kPetPackRecordBytes)) return false;
  header = candidate;
  return true;
}

inline uint32_t pet_frame_crc(const uint8_t *pixels, size_t size) {
  static constexpr auto table = [] {
    std::array<uint32_t, 256> values{};
    for (size_t i = 0; i < values.size(); ++i) {
      auto c = static_cast<uint32_t>(i);
      for (int b = 0; b < 8; ++b) c = (c >> 1) ^ ((c & 1) ? 0xedb88320U : 0U);
      values[i] = c;
    }
    return values;
  }();
  uint32_t crc = 0xffffffffU;
  for (size_t i = 0; i < size; ++i)
    crc = (crc >> 8) ^ table[(crc ^ pixels[i]) & 255U];
  return crc ^ 0xffffffffU;
}

inline bool read_pet_frame_at(FILE *file, long offset, uint16_t *pixels) {
  uint8_t check[4];
  auto *bytes = reinterpret_cast<uint8_t *>(pixels);
  if (offset < 0 || fseek(file, offset, SEEK_SET) != 0 ||
      fread(check, 1, sizeof(check), file) != sizeof(check) ||
      fread(bytes, 1, kPetSpriteBytes, file) != kPetSpriteBytes) return false;
  const uint32_t expected = uint32_t(check[0]) | (uint32_t(check[1]) << 8) |
      (uint32_t(check[2]) << 16) | (uint32_t(check[3]) << 24);
  return expected == pet_frame_crc(bytes, kPetSpriteBytes);
}

inline bool read_pet_pack_frame(FILE *file, const PetPackHeader &header, PetClip clip,
                                unsigned index, uint16_t *pixels) {
  if (header.sharded()) return false;
  return read_pet_frame_at(file, header.frame_offset(clip, index), pixels);
}

inline bool read_pet_shard_frame(FILE *file, const PetPackHeader &header, PetClip clip,
                                unsigned index, uint16_t *pixels) {
  const auto slot = static_cast<unsigned>(clip);
  if (!header.sharded() || slot >= header.counts.size() || index >= header.counts[slot]) return false;
  const auto offset = static_cast<long>(index % kPetFramesPerShard * kPetPackRecordBytes);
  const auto remaining = header.counts[slot] - index / kPetFramesPerShard * kPetFramesPerShard;
  const auto frames = remaining < kPetFramesPerShard ? remaining : kPetFramesPerShard;
  // The tail shard can have fewer than twelve frames; reject incomplete data
  // without walking beyond the fixed-size shard.
  if (!pet_file_has_size(file, frames * kPetPackRecordBytes)) return false;
  return read_pet_frame_at(file, offset, pixels);
}

} // namespace smart_speaker::product
