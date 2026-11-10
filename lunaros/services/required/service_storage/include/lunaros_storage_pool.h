#pragma once

#include "lunaros_storage_quota.h"
#include <cstddef>
#include <string>

namespace lunaredu::lunaros {

inline constexpr size_t kStoragePoolBytes = 0x3e0000;
inline constexpr size_t kStoragePoolHeadroom = 256 * 1024;

// Versioned shared-pool layouts. Smaller legacy volumes are not managed pools.
inline constexpr bool storage_pool_supported_size(size_t bytes) {
  return bytes == kStoragePoolBytes || bytes == 0x5e0000 || bytes == 0x7e0000 || bytes == 0x760000;
}
inline constexpr size_t storage_pool_headroom(size_t bytes) {
  return bytes == 0x5e0000 ? 128 * 1024 : kStoragePoolHeadroom;
}

// Callers pass a path already canonicalized by Storage.
bool storage_pool_managed_path(const std::string &path);

bool storage_pool_admit(size_t bytes, bool managed, std::string &error);

} // namespace lunaredu::lunaros
