#include "persistence/storage/littlefs_path.h"

#include <mutex>
#include <string>
#include <sys/stat.h>

#include "littlefs_store_internal.h"

namespace {

LittlefsStoreState &state = littlefs_store_state();

} // namespace

std::string littlefs_store_normalize_path(const char *path) {
  return littlefs_internal_normalize_path(path);
}

bool littlefs_store_is_safe_path(const char *path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  std::string normalized;
  return littlefs_internal_canonicalize_path(path, normalized) &&
         !normalized.empty() && normalized[0] == '/' &&
         !littlefs_internal_transaction_path(normalized);
}

bool littlefs_store_ensure_directory(const char *path) {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  if (!state.ready || path == nullptr || path[0] != '/' ||
      !littlefs_store_is_safe_path(path)) {
    return false;
  }
  return littlefs_internal_ensure_directory_recursive(path);
}

bool littlefs_store_path_exists(const char *path) {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  if (!state.ready || path == nullptr || path[0] != '/' ||
      !littlefs_store_is_safe_path(path)) {
    return false;
  }

  const std::string normalized = littlefs_internal_normalize_path(path);
  if (normalized == "/") {
    return true;
  }
  if (normalized.empty()) {
    return false;
  }

  const std::string host_path =
      littlefs_internal_host_path(normalized.c_str());
  struct stat info;
  return ::stat(host_path.c_str(), &info) == 0;
}
