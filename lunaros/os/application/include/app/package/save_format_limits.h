#pragma once
#include <cstddef>
#include <cstdint>
namespace lunaredu::lunaros::apps {
inline constexpr size_t kSaveFileHeaderBytes = 24;
inline constexpr size_t kSaveFileIdMaxBytes = 47;
inline constexpr size_t save_file_maximum_bytes(size_t maximum_save_bytes) {
  constexpr size_t overhead = kSaveFileHeaderBytes + kSaveFileIdMaxBytes;
  return maximum_save_bytes > SIZE_MAX - overhead ? SIZE_MAX : overhead + maximum_save_bytes;
}
}
