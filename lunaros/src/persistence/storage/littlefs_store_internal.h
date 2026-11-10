#pragma once

#include <cstddef>
#include <array>
#include <mutex>
#include <string>

inline constexpr const char *kLittlefsMountPath = "/littlefs";
inline constexpr const char *kLittlefsPartitionLabel = "filesystem";
inline constexpr const char *kLittlefsTransactionsPath = "/.lunaros-transactions";

inline bool littlefs_internal_transaction_path(const std::string &path) {
  const std::string base(kLittlefsTransactionsPath);
  return path == base || path.compare(0, base.size() + 1, base + "/") == 0;
}

struct LittlefsStoreState {
  std::recursive_mutex mutex;
  std::string last_error;
  size_t total_bytes = 0;
  size_t used_bytes = 0;
  bool ready = false;
  struct Stream {
    const std::string *path = nullptr;
    bool writing = false;
  };
  // Borrowed paths remain alive until their stack-owned lease is released.
  // Metadata is protected by mutex; network/user callbacks never hold it.
  std::array<Stream, 8> streams{};
  std::array<bool, 8> write_slots{};
};

LittlefsStoreState &littlefs_store_state();

std::string littlefs_internal_errno_error(const char *prefix);
bool littlefs_internal_fail(std::string *error, const char *message);
bool littlefs_internal_fail(std::string *error, const std::string &message);
bool littlefs_internal_canonicalize_path(const char *path,
                                         std::string &normalized);
std::string littlefs_internal_normalize_path(const char *path);
std::string littlefs_internal_host_path(const char *path);
bool littlefs_internal_ensure_directory_recursive(const char *path);
bool littlefs_internal_refresh_usage();
bool littlefs_internal_cleanup_writes();

// Caller holds the filesystem mutex. Mutations also protect parent directories.
inline bool littlefs_internal_stream_conflicts(const std::string &path,
                                                bool writing) {
  for (const auto &stream : littlefs_store_state().streams) {
    if (stream.path == nullptr || (!writing && !stream.writing)) continue;
    const auto &active = *stream.path;
    if (active == path || (writing && active.size() > path.size() &&
        active.compare(0, path.size(), path) == 0 && active[path.size()] == '/')) {
      return true;
    }
  }
  return false;
}

class LittlefsStreamLease {
public:
  LittlefsStreamLease() = default;
  LittlefsStreamLease(const LittlefsStreamLease &) = delete;
  LittlefsStreamLease &operator=(const LittlefsStreamLease &) = delete;
  bool acquire(const std::string &path, bool writing, std::string *error) {
    auto &state = littlefs_store_state();
    if (littlefs_internal_stream_conflicts(path, writing)) {
      return littlefs_internal_fail(error, "file_busy");
    }
    for (auto &stream : state.streams) {
      if (stream.path == nullptr) {
        stream = {&path, writing};
        slot_ = &stream;
        return true;
      }
    }
    return littlefs_internal_fail(error, "storage_stream_capacity_exhausted");
  }
  ~LittlefsStreamLease() {
    if (slot_ != nullptr) {
      std::lock_guard<std::recursive_mutex> lock(littlefs_store_state().mutex);
      *slot_ = {};
    }
  }
private:
  LittlefsStoreState::Stream *slot_ = nullptr;
};
