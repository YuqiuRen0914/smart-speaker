#pragma once

#include "lunaros_storage_pool.h"
#include <cstdint>

namespace lunaredu::lunaros {

inline constexpr size_t kStoragePoolDefaultUserReserve = 512 * 1024;

inline constexpr size_t storage_pool_default_user_reserve(size_t total,
                                                         size_t configured = 0) {
  const auto headroom = storage_pool_headroom(total);
  if (configured && total >= headroom && configured <= total - headroom)
    return configured;
  return total == 0x5e0000 ? 64 * 1024 : kStoragePoolDefaultUserReserve;
}

// One conservative allocation estimate for preflight and live reservations.
inline size_t storage_pool_allocation_bytes(size_t bytes) {
  if(bytes>SIZE_MAX-4095)return SIZE_MAX;
  const auto blocks=(bytes+4095)/4096;
  if(blocks>SIZE_MAX/4096-4)return SIZE_MAX;
  const auto rounded=(blocks+4)*4096,ctz=bytes/128;
  return ctz>SIZE_MAX-rounded?SIZE_MAX:rounded+ctz;
}

inline bool storage_pool_fits(size_t total, size_t used, size_t allocation,
                              size_t user_used, size_t user_reserve) {
  const size_t reserved =
      user_used < user_reserve ? user_reserve - user_used : 0;
  const auto headroom = storage_pool_headroom(total);
  return used <= total && allocation <= total - used &&
         headroom <= total - used - allocation &&
         reserved <= total - used - allocation - headroom;
}

} // namespace lunaredu::lunaros
