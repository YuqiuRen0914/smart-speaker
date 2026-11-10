#include "littlefs_store_internal.h"

#include <cerrno>
#include <cstring>
#include <sys/stat.h>

#include <vector>

#include <esp_err.h>
#include <esp_littlefs.h>

LittlefsStoreState &littlefs_store_state() {
  static LittlefsStoreState state;
  return state;
}

std::string littlefs_internal_errno_error(const char *prefix) {
  const int captured_errno = errno;
  return std::string(prefix) + ":" + std::to_string(captured_errno) + ":" +
         strerror(captured_errno);
}

bool littlefs_internal_fail(std::string *error, const char *message) {
  LittlefsStoreState &state = littlefs_store_state();
  state.last_error = message == nullptr ? "" : message;
  if (error != nullptr) {
    *error = state.last_error;
  }
  return false;
}

bool littlefs_internal_fail(std::string *error, const std::string &message) {
  LittlefsStoreState &state = littlefs_store_state();
  state.last_error = message;
  if (error != nullptr) {
    *error = message;
  }
  return false;
}

bool littlefs_internal_canonicalize_path(const char *path,
                                         std::string &normalized) {
  normalized.clear();
  if (path == nullptr || path[0] == '\0' || strcmp(path, "/") == 0) {
    normalized = "/";
    return true;
  }

  std::vector<std::string> segments;
  const std::string input(path);
  size_t index = 0;
  while (index < input.length()) {
    while (index < input.length() &&
           (input[index] == '/' || input[index] == '\\')) {
      ++index;
    }
    if (index >= input.length()) {
      break;
    }

    const size_t start = index;
    while (index < input.length() && input[index] != '/' &&
           input[index] != '\\') {
      ++index;
    }
    const std::string segment = input.substr(start, index - start);
    if (segment.empty() || segment == ".") {
      continue;
    }
    if (segment == "..") {
      if (segments.empty()) {
        return false;
      }
      segments.pop_back();
      continue;
    }
    segments.push_back(segment);
  }

  normalized = "/";
  for (size_t index = 0; index < segments.size(); ++index) {
    if (index > 0) {
      normalized += "/";
    }
    normalized += segments[index];
  }
  return true;
}

std::string littlefs_internal_normalize_path(const char *path) {
  std::string normalized;
  if (!littlefs_internal_canonicalize_path(path, normalized)) {
    return "";
  }
  return normalized;
}

std::string littlefs_internal_host_path(const char *path) {
  const std::string normalized = littlefs_internal_normalize_path(path);
  if (normalized.empty()) {
    return "";
  }
  if (normalized == "/") {
    return kLittlefsMountPath;
  }
  return std::string(kLittlefsMountPath) + normalized;
}

bool littlefs_internal_ensure_directory_recursive(const char *path) {
  const std::string normalized = littlefs_internal_normalize_path(path);
  if (normalized.empty()) {
    return false;
  }
  if (normalized == "/") {
    return true;
  }

  for (size_t index = 1; index <= normalized.length(); ++index) {
    const bool at_end = index == normalized.length();
    const bool is_separator = !at_end && normalized[index] == '/';
    if (!at_end && !is_separator) {
      continue;
    }

    const std::string current = normalized.substr(0, index);
    if (current.empty()) {
      continue;
    }

    const std::string host_path =
        littlefs_internal_host_path(current.c_str());
    struct stat info;
    if (::stat(host_path.c_str(), &info) == 0) {
      continue;
    }
    if (::mkdir(host_path.c_str(), 0755) != 0 && errno != EEXIST) {
      return false;
    }
  }
  return true;
}

bool littlefs_internal_refresh_usage() {
  LittlefsStoreState &state = littlefs_store_state();
  if (!state.ready) {
    return false;
  }

  const esp_err_t result = esp_littlefs_info(
      kLittlefsPartitionLabel, &state.total_bytes, &state.used_bytes);
  if (result != ESP_OK) {
    state.last_error = esp_err_to_name(result);
    return false;
  }
  state.last_error.clear();
  return true;
}
