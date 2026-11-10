#pragma once
#include "lunaros_crc32_c.h"
namespace lunaredu::lunaros::digest {
// IEEE CRC-32; the empty sequence yields zero. No allocation or platform state.
inline uint32_t crc32(const void *data, size_t size) {
  return lunaros_crc32_update(0,data,size);
}
}
