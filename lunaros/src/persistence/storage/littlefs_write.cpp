#include "persistence/storage/littlefs_write.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "persistence/storage/littlefs_path.h"
#include "littlefs_store_internal.h"
#include "lunaredu_lunaros/persistence/storage_pool.h"

namespace {
LittlefsStoreState &state = littlefs_store_state();

bool replace_file(const std::string &source, const std::string &target) {
#ifdef _WIN32
  // The host CRT rename cannot replace an existing file. Match LittleFS/POSIX
  // atomic replacement, never delete the destination before publishing.
  return MoveFileExA(source.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  return std::rename(source.c_str(), target.c_str()) == 0;
#endif
}

std::string transaction_path(size_t slot) {
  return littlefs_internal_host_path(
      (std::string(kLittlefsTransactionsPath) + "/" + std::to_string(slot)).c_str());
}

struct WriteTransaction {
  FILE *file = nullptr;
  size_t slot = state.write_slots.size();
  size_t bytes_written = 0;
  size_t user_reserve = 0;
  bool failed = false;
  std::string path;

  bool open() {
    if (!littlefs_internal_ensure_directory_recursive(kLittlefsTransactionsPath)) return false;
    for (size_t i = 0; i < state.write_slots.size(); ++i) {
      if (state.write_slots[i]) continue;
      slot = i;
      state.write_slots[i] = true;
      path = transaction_path(i);
      file = std::fopen(path.c_str(), "wb");
      return file != nullptr;
    }
    return false;
  }

  bool commit(const std::string &target, std::string &error) {
    bool ok = !failed;
    if (std::fflush(file) != 0) ok = false;
#ifndef _WIN32
    if (ok && fsync(fileno(file)) != 0) ok = false;
#endif
    if (std::fclose(file) != 0) ok = false;
    file = nullptr;
    if (!ok) { error = "flush_file_failed"; return false; }
    if (!replace_file(path, target)) { error = "publish_file_failed"; return false; }
    path.clear();
    return true;
  }

  ~WriteTransaction() {
    if (file) std::fclose(file);
    if (!path.empty()) std::remove(path.c_str());
    if (slot < state.write_slots.size()) state.write_slots[slot] = false;
  }
};

bool write_chunk(const uint8_t *data, size_t size, void *context) {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  auto &write = *static_cast<WriteTransaction *>(context);
  if (write.failed) return false;
  if (size && !data) { write.failed = true; return false; }
  if (!size) return true;
  if (size > SIZE_MAX - write.bytes_written) { write.failed = true; return false; }
  if (state.total_bytes == lunaredu::lunaros::kStoragePoolBytes) {
    littlefs_internal_refresh_usage();
    if (size > state.total_bytes ||
        !lunaredu::lunaros::storage_pool_fits(state.total_bytes, state.used_bytes,
          size + 16384, 0, write.user_reserve)) {
      write.failed = true;
      return false;
    }
  }
  const size_t written = std::fwrite(data, 1, size, write.file);
  write.bytes_written += written;
  write.failed = written != size;
  return !write.failed;
}

bool ensure_parent_directory(const std::string &normalized) {
  const auto slash = normalized.find_last_of('/');
  return slash == std::string::npos || slash == 0 ||
      littlefs_internal_ensure_directory_recursive(normalized.substr(0, slash).c_str());
}
} // namespace

// Called after mount and before accepting clients. Only these eight reserved
// transaction files belong to Storage; never enumerate or delete user paths.
bool littlefs_internal_cleanup_writes() {
  for (size_t slot = 0; slot < state.write_slots.size(); ++slot) {
    if (state.write_slots[slot]) return littlefs_internal_fail(nullptr, "storage_stream_active");
    if (std::remove(transaction_path(slot).c_str()) != 0 && errno != ENOENT)
      return littlefs_internal_fail(nullptr, "transaction_cleanup_failed");
  }
  return true;
}

bool littlefs_store_write_text_file(const char *path, const char *content) {
  return littlefs_store_write_text_file_ex(path, content, nullptr);
}

bool littlefs_store_write_text_file_ex(const char *path, const char *content,
                                     size_t *written_bytes) {
  const char *body = content ? content : "";
  return littlefs_store_write_file(path, reinterpret_cast<const uint8_t *>(body),
                                   std::strlen(body), written_bytes);
}

bool littlefs_store_write_file(const char *path, const uint8_t *data,
                              size_t size, size_t *written_bytes) {
  if (size && !data) {
    if (written_bytes) *written_bytes = 0;
    return false;
  }
  struct Buffer { const uint8_t *data; size_t size; } buffer{data, size};
  return littlefs_store_write_streamed_file(path,
      [](StorageFileChunkWriter writer, void *target, void *context, std::string &) {
        const auto &buffer = *static_cast<Buffer *>(context);
        return writer(buffer.data, buffer.size, target);
      }, &buffer, written_bytes, nullptr);
}

bool littlefs_store_write_streamed_file(
    const char *path, StorageWriteStreamProducer producer, void *producer_context,
    size_t *written_bytes, std::string *error) {
  std::unique_lock<std::recursive_mutex> lock(state.mutex);
  if (written_bytes) *written_bytes = 0;
  if (!state.ready) return littlefs_internal_fail(error, "storage_not_ready");
  if (!path || path[0] != '/' || !littlefs_store_is_safe_path(path) || !producer)
    return littlefs_internal_fail(error, "invalid_path");
  const auto normalized = littlefs_internal_normalize_path(path);
  if (normalized.empty() || normalized == "/") return littlefs_internal_fail(error, "invalid_path");
  LittlefsStreamLease lease;
  if (!lease.acquire(normalized, true, error)) return false;
  if (!ensure_parent_directory(normalized))
    return littlefs_internal_fail(error, "create_parent_directory_failed");

  WriteTransaction write;
  if (!write.open()) return littlefs_internal_fail(error, "open_transaction_failed");
  if (state.total_bytes == lunaredu::lunaros::kStoragePoolBytes &&
      (lunaredu::lunaros::storage_pool_managed_path(normalized) ||
       normalized.rfind("/resources/", 0) == 0 || normalized.rfind("/cache/", 0) == 0)) {
    const auto used = lunaredu::lunaros::storage_pool_user_used();
    const auto reserve = lunaredu::lunaros::storage_pool_user_reserve();
    write.user_reserve = used < reserve ? reserve - used : 0;
  }
  std::string failure;
  lock.unlock();
  const bool produced = producer(write_chunk, &write, producer_context, failure);
  lock.lock();
  if (!produced || write.failed ||
      !write.commit(littlefs_internal_host_path(normalized.c_str()), failure))
    return littlefs_internal_fail(error, failure.empty() ? "write_file_failed" : failure);
  if (written_bytes) *written_bytes = write.bytes_written;
  littlefs_internal_refresh_usage();
  state.last_error.clear();
  if (error) error->clear();
  return true;
}

bool littlefs_store_delete_file(const char *path) {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  if (!state.ready || path == nullptr || path[0] != '/' ||
      !littlefs_store_is_safe_path(path) || !littlefs_store_path_exists(path)) {
    return false;
  }

  const std::string normalized = littlefs_internal_normalize_path(path);
  if (littlefs_internal_stream_conflicts(normalized, true)) {
    return littlefs_internal_fail(nullptr, "file_busy");
  }
  if (normalized.empty() || normalized == "/") {
    return false;
  }

  const std::string host_path =
      littlefs_internal_host_path(normalized.c_str());
  const bool ok = remove(host_path.c_str()) == 0;
  if (ok) {
    littlefs_internal_refresh_usage();
  }
  return ok;
}

bool littlefs_store_move_file(const char *source_path,
                              const char *destination_path,
                              std::string *error) {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  if (!state.ready || source_path == nullptr || destination_path == nullptr ||
      source_path[0] != '/' || destination_path[0] != '/' ||
      !littlefs_store_is_safe_path(source_path) ||
      !littlefs_store_is_safe_path(destination_path)) {
    return littlefs_internal_fail(error, "invalid_path");
  }

  const std::string source = littlefs_internal_normalize_path(source_path);
  const std::string destination =
      littlefs_internal_normalize_path(destination_path);
  if (littlefs_internal_stream_conflicts(source, true) ||
      littlefs_internal_stream_conflicts(destination, true)) {
    return littlefs_internal_fail(error, "file_busy");
  }
  if (source.empty() || destination.empty() || source == destination ||
      !littlefs_store_path_exists(source.c_str())) {
    return littlefs_internal_fail(error, "invalid_path");
  }
  if (!ensure_parent_directory(destination)) {
    return littlefs_internal_fail(error,
                                  "create_parent_directory_failed");
  }

  const std::string source_host =
      littlefs_internal_host_path(source.c_str());
  const std::string destination_host =
      littlefs_internal_host_path(destination.c_str());
  if (!replace_file(source_host, destination_host)) {
    return littlefs_internal_fail(
        error, littlefs_internal_errno_error("move_file_failed"));
  }

  state.last_error.clear();
  littlefs_internal_refresh_usage();
  if (error != nullptr) {
    error->clear();
  }
  return true;
}
