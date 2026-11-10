#include "persistence/storage/littlefs_read.h"
#include <climits>

#include <cstdio>
#include <mutex>
#include <string>
#include <vector>
#include <sys/stat.h>

#include "persistence/storage/littlefs_path.h"
#include "littlefs_store_internal.h"

bool littlefs_store_read_range(const char *path, size_t offset, uint8_t *data, size_t size) {
  auto &store = littlefs_store_state();
  std::lock_guard<std::recursive_mutex> lock(store.mutex);
  if (!store.ready || !data || offset > LONG_MAX || !littlefs_store_is_safe_path(path)) return false;
  const auto normalized = littlefs_internal_normalize_path(path);
  if (normalized.empty() || normalized == "/" || littlefs_internal_stream_conflicts(normalized, false)) return false;
  FILE *file = std::fopen(littlefs_internal_host_path(normalized.c_str()).c_str(), "rb");
  if (!file) return false;
  bool ok = std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0 && std::fread(data, 1, size, file) == size;
  if (std::fclose(file) != 0) ok = false;
  return ok;
}

namespace {

LittlefsStoreState &state = littlefs_store_state();

template <typename Buffer>
bool read_file_into(const char *path, Buffer &content) {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  content.clear();
  if (!state.ready || path == nullptr || path[0] != '/' ||
      !littlefs_store_is_safe_path(path) || !littlefs_store_path_exists(path)) {
    return false;
  }

  const std::string normalized = littlefs_internal_normalize_path(path);
  if (normalized.empty() || normalized == "/") {
    return false;
  }
  if (littlefs_internal_stream_conflicts(normalized, false)) {
    return littlefs_internal_fail(nullptr, "file_busy");
  }

  const std::string host_path =
      littlefs_internal_host_path(normalized.c_str());
  FILE *file = fopen(host_path.c_str(), "rb");
  if (file == nullptr) {
    return false;
  }

  struct stat info{};
  if (fstat(fileno(file), &info) != 0 || info.st_size < 0 ||
      static_cast<uint64_t>(info.st_size) > content.max_size()) {
    fclose(file);
    return littlefs_internal_fail(nullptr, "file_size_unavailable");
  }
  content.resize(static_cast<size_t>(info.st_size));
  const size_t read_count = content.empty() ? 0 :
      fread(content.data(), 1, content.size(), file);
  if (read_count != content.size() || ferror(file) != 0) {
    content.clear();
    state.last_error = littlefs_internal_errno_error("read_file_failed");
    fclose(file);
    return false;
  }
  if (fclose(file) != 0) {
    content.clear();
    state.last_error = littlefs_internal_errno_error("close_file_failed");
    return false;
  }
  state.last_error.clear();
  return true;
}

} // namespace

bool littlefs_store_read_text_file(const char *path, std::string &content) {
  return read_file_into(path, content);
}

bool littlefs_store_read_file(const char *path, std::vector<uint8_t> &content) {
  return read_file_into(path, content);
}

bool littlefs_store_stream_file(const char *path, StorageFileChunkWriter writer,
                                void *context, std::string *error) {
  std::unique_lock<std::recursive_mutex> lock(state.mutex);
  if (!state.ready) {
    return littlefs_internal_fail(error, "storage_not_ready");
  }
  if (path == nullptr || path[0] != '/' || !littlefs_store_is_safe_path(path) ||
      writer == nullptr) {
    return littlefs_internal_fail(error, "invalid_path");
  }

  const std::string normalized = littlefs_internal_normalize_path(path);
  if (normalized.empty() || normalized == "/") {
    return littlefs_internal_fail(error, "invalid_path");
  }
  LittlefsStreamLease lease;
  if (!lease.acquire(normalized, false, error)) return false;

  const std::string host_path =
      littlefs_internal_host_path(normalized.c_str());
  FILE *file = fopen(host_path.c_str(), "rb");
  if (file == nullptr) {
    return littlefs_internal_fail(
        error, littlefs_internal_errno_error("open_file_failed"));
  }

  bool ok = true;
  uint8_t buffer[1024];
  while (true) {
    const size_t read_count = fread(buffer, 1, sizeof(buffer), file);
    lock.unlock();
    const bool accepted = read_count == 0 || writer(buffer, read_count, context);
    lock.lock();
    if (!accepted) {
      ok = false;
      littlefs_internal_fail(error, "file_stream_cancelled");
      break;
    }
    if (read_count < sizeof(buffer)) {
      if (ferror(file) != 0) {
        ok = false;
        littlefs_internal_fail(
            error, littlefs_internal_errno_error("read_file_failed"));
      }
      break;
    }
  }

  fclose(file);
  return ok;
}
