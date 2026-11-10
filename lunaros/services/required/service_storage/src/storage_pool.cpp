#include "storage_pool.h"
#include "lunaros_storage.h"
#include "configuration/settings.h"
#include <charconv>
#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

namespace lunaredu::lunaros {

bool storage_pool_enabled() { return storage_pool_supported_size(storage_total_bytes()); }
size_t storage_pool_user_reserve() {
  std::string text, error;
  // An unreadable reserve must not allow managed downloads to borrow user space.
  if (settings_read_string("lunar_pool", "user_reserve", 32, text, error) ==
      SettingsReadResult::Failed) return storage_total_bytes() - storage_pool_headroom(storage_total_bytes());
  const size_t default_reserve = storage_pool_default_user_reserve(storage_total_bytes(),
#if CONFIG_LUNAREDU_STORAGE_POOL_USER_RESERVE_KIB
      static_cast<size_t>(CONFIG_LUNAREDU_STORAGE_POOL_USER_RESERVE_KIB) * 1024
#else
      0
#endif
      );
  size_t value = default_reserve;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc() && result.ptr == text.data() + text.size() &&
      value <= storage_total_bytes() - storage_pool_headroom(storage_total_bytes())
      ? value : default_reserve;
}
size_t storage_pool_user_used() {
  size_t used = 0;
  auto visitor = [](const StorageListEntry &entry, void *context) {
    if (!entry.is_directory && !storage_pool_managed_path(entry.path) &&
        entry.path.rfind("/cache/", 0) != 0 && entry.path.rfind("/resources/", 0) != 0) {
      *static_cast<size_t *>(context) += (entry.size_bytes + 4095) / 4096 * 4096;
    }
    return true;
  };
  // A failed scan must not let resources borrow an unaccounted user reserve.
  if (!storage_list_entries_recursive("/", 16, visitor, &used)) return 0;
  return used;
}
bool storage_pool_set_user_reserve(size_t bytes, std::string &error) {
  if (!storage_pool_enabled() || bytes > storage_total_bytes() - storage_pool_headroom(storage_total_bytes())) {
    error = "invalid_user_reserve"; return false;
  }
  if (!storage_refresh_usage() || !storage_pool_fits(storage_total_bytes(),
      storage_used_bytes(), 0, storage_pool_user_used(), bytes)) {
    error = "user_reserve_unavailable"; return false;
  }
  if (!settings_set_string("lunar_pool", "user_reserve", std::to_string(bytes))) {
    error = "user_reserve_save_failed"; return false;
  }
  return true;
}
bool storage_pool_admit(size_t bytes, bool managed, std::string &error) {
  if (!storage_ready() || !storage_refresh_usage()) {
    error = "storage_not_ready"; return false;
  }
  if (!storage_pool_enabled()) return true;
  // CTZ pointers, metadata and block rounding; use a conservative estimate.
  if (bytes > storage_total_bytes()) { error = "storage_pool_full"; return false; }
  const size_t allocation = storage_pool_allocation_bytes(bytes);
  if (!storage_pool_fits(storage_total_bytes(), storage_used_bytes(), allocation,
      managed ? storage_pool_user_used() : 0,
      managed ? storage_pool_user_reserve() : 0)) {
    error = "storage_pool_full"; return false;
  }
  return true;
}
} // namespace lunaredu::lunaros
