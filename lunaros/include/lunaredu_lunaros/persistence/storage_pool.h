#pragma once
#include <cstddef>
#include <string>

namespace lunaredu::lunaros {
inline constexpr size_t kStoragePoolBytes = 0x3e0000;
inline constexpr size_t kStoragePoolHeadroom = 256 * 1024;
inline constexpr size_t kStoragePoolDefaultUserReserve = 512 * 1024;
// Paths must already have passed storage canonicalization.
inline bool storage_pool_managed_path(const std::string &path) {
  return path == "/managed" || path.rfind("/managed/", 0) == 0;
}
inline bool storage_pool_fits(size_t total, size_t used, size_t allocation,
                              size_t user_used, size_t user_reserve) {
  const size_t reserved = user_used < user_reserve ? user_reserve - user_used : 0;
  return used <= total && allocation <= total - used &&
      kStoragePoolHeadroom <= total - used - allocation &&
      reserved <= total - used - allocation - kStoragePoolHeadroom;
}
bool storage_pool_enabled();
size_t storage_pool_user_reserve();
bool storage_pool_set_user_reserve(size_t bytes, std::string &error);
size_t storage_pool_user_used();
bool storage_pool_admit(size_t bytes, bool managed, std::string &error);
} // namespace lunaredu::lunaros
