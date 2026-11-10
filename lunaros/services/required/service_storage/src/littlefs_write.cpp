#include "kernel/platform_operation.h"
#include "lunaros_storage.h"
#include "lunaros_storage_private_paths.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <optional>
#include <limits>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "littlefs_store_internal.h"

using lunaredu::lunaros::StorageFileChunkWriter;
using lunaredu::lunaros::StorageWriteStreamProducer;
#include "storage_pool.h"

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

size_t other_write_reservations(size_t except) {
  size_t total=0;
  for(size_t i=0;i<state.write_reservations.size();++i)if(i!=except){
    const auto value=state.write_reservations[i];
    if(value>SIZE_MAX-total)return SIZE_MAX;
    total+=value;
  }
  return total;
}
bool capacity_fits(size_t allocation,size_t except,size_t user_reserve) {
  const auto reserved=other_write_reservations(except);
  return allocation<=SIZE_MAX-reserved &&
    lunaredu::lunaros::storage_pool_fits(state.total_bytes,state.used_bytes,
        allocation+reserved,0,user_reserve);
}

struct WriteTransaction {
  FILE *file = nullptr;
  size_t slot = state.write_slots.size();
  size_t bytes_written = 0;
  size_t user_reserve = 0;
  size_t reservation_overhead = 0;
  std::optional<size_t> expected_bytes;
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

  bool reserve(size_t expected) {
    if(lunaredu::lunaros::storage_pool_supported_size(state.total_bytes)) {
      const auto allocation=lunaredu::lunaros::storage_pool_allocation_bytes(expected);
      if(!littlefs_internal_refresh_usage() || allocation==SIZE_MAX ||
         !capacity_fits(allocation,slot,user_reserve))return false;
      state.write_reservations[slot]=allocation;reservation_overhead=allocation-expected;
    }
    expected_bytes=expected;
    return true;
  }

  bool commit(const std::string &target, std::string &error) {
    if(expected_bytes&&bytes_written!=*expected_bytes){error="storage_write_incomplete";return false;}
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
    if (slot < state.write_slots.size()) {state.write_reservations[slot]=0;state.write_slots[slot] = false;}
  }
};

bool write_chunk(const uint8_t *data, size_t size, void *context) {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  auto &write = *static_cast<WriteTransaction *>(context);
  if (write.failed) return false;
  if (size && !data) { write.failed = true; return false; }
  if (!size) return true;
  if (size > SIZE_MAX - write.bytes_written) { write.failed = true; return false; }
  if(write.expected_bytes) {
    if(write.bytes_written>*write.expected_bytes || size>*write.expected_bytes-write.bytes_written){write.failed=true;return false;}
  } else if (lunaredu::lunaros::storage_pool_supported_size(state.total_bytes)) {
    if(!littlefs_internal_refresh_usage() || size>SIZE_MAX-16384 ||
       !capacity_fits(size+16384,write.slot,write.user_reserve)) {
      write.failed = true;
      return false;
    }
  }
  const size_t written = std::fwrite(data, 1, size, write.file);
  write.bytes_written += written;
  if(write.expected_bytes&&state.write_reservations[write.slot])
    state.write_reservations[write.slot]=*write.expected_bytes-write.bytes_written+write.reservation_overhead;
  write.failed = written != size;
  return !write.failed;
}

bool ensure_parent_directory(const std::string &normalized) {
  const auto slash = normalized.find_last_of('/');
  return slash == std::string::npos || slash == 0 ||
      littlefs_internal_ensure_directory_recursive(normalized.substr(0, slash).c_str());
}

struct IncrementalWrite {
  uint64_t id=0;
  size_t expected=0;
  std::string target;
  LittlefsStreamLease lease;
  std::optional<WriteTransaction> transaction;
  void release(){transaction.reset();lease.release();target.clear();id=0;expected=0;}
};
std::array<IncrementalWrite,2> incremental_writes;
uint64_t next_incremental_write=0;
IncrementalWrite *find_write(lunaredu::lunaros::StorageWriteHandle handle){
  if(handle)for(auto &write:incremental_writes)if(write.id==handle.id)return &write;
  return nullptr;
}
} // namespace

bool lunaredu::lunaros::storage_begin_write(const char *path,size_t expected,StorageWriteHandle &handle,std::string &error){
  PlatformOperation operation(execution_effect(ExecutionEffect::FlashWrite));
  if(!operation.accepted()){error="storage_execution_denied";return false;}
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  if(handle){error="storage_write_handle_active";return false;}
  if(!state.ready)return littlefs_internal_fail(&error,"storage_not_ready");
  if(!path||path[0]!='/'||!storage_is_safe_path(path)||!expected)return littlefs_internal_fail(&error,"invalid_write_request");
  if(next_incremental_write==std::numeric_limits<uint64_t>::max())return littlefs_internal_fail(&error,"storage_write_generation_exhausted");
  const auto normalized=littlefs_internal_normalize_path(path);
  if(normalized.empty()||normalized=="/")return littlefs_internal_fail(&error,"invalid_path");
  IncrementalWrite *slot=nullptr;for(auto &candidate:incremental_writes)if(!candidate.id&&!candidate.transaction){slot=&candidate;break;}
  if(!slot)return littlefs_internal_fail(&error,"storage_write_capacity_exhausted");
  slot->target=normalized;
  if(!slot->lease.acquire(slot->target,true,&error)){slot->target.clear();return false;}
  slot->transaction.emplace();
  auto fail=[&](const char *why){slot->release();return littlefs_internal_fail(&error,why);};
  if(!ensure_parent_directory(normalized)||!slot->transaction->open())return fail("open_transaction_failed");
  if(storage_pool_supported_size(state.total_bytes)){
    const auto used=storage_pool_user_used(),reserve=storage_pool_user_reserve();
    // This path is native and private, but it must preserve the user quota.
    if(storage_private_path(normalized)||storage_pool_managed_path(normalized)||normalized.rfind("/resources/",0)==0||normalized.rfind("/cache/",0)==0)
      slot->transaction->user_reserve=used<reserve?reserve-used:0;
  }
  if(!slot->transaction->reserve(expected))return fail("storage_write_no_space");
  slot->id=++next_incremental_write;slot->expected=expected;handle.id=slot->id;error.clear();return true;
}
bool lunaredu::lunaros::storage_append_write(StorageWriteHandle handle,size_t offset,const uint8_t *data,size_t size,std::string &error){
  PlatformOperation operation(execution_effect(ExecutionEffect::FlashWrite));
  if(!operation.accepted()){error="storage_execution_denied";return false;}
  std::lock_guard<std::recursive_mutex> lock(state.mutex);auto *slot=find_write(handle);
  if(!slot)return littlefs_internal_fail(&error,"storage_write_handle_stale");
  auto &write=*slot->transaction;
  if(!data||!size||size>4096||offset!=write.bytes_written||offset>slot->expected||size>slot->expected-offset)
    return littlefs_internal_fail(&error,"storage_write_offset_or_size_invalid");
  if(!write_chunk(data,size,&write))return littlefs_internal_fail(&error,"storage_write_failed");
  error.clear();return true;
}
bool lunaredu::lunaros::storage_commit_write(StorageWriteHandle &handle,std::string &error){
  PlatformOperation operation(execution_effect(ExecutionEffect::FlashWrite));
  if(!operation.accepted()){error="storage_execution_denied";return false;}
  std::lock_guard<std::recursive_mutex> lock(state.mutex);auto *slot=find_write(handle);
  if(!slot)return littlefs_internal_fail(&error,"storage_write_handle_stale");
  auto &write=*slot->transaction;
  if(write.bytes_written!=slot->expected||write.failed||!write.file)return littlefs_internal_fail(&error,"storage_write_incomplete");
  if(!write.commit(littlefs_internal_host_path(slot->target.c_str()),error))return false;
  slot->release();handle={};littlefs_internal_refresh_usage();error.clear();return true;
}
bool lunaredu::lunaros::storage_abort_write(StorageWriteHandle &handle,std::string &error){
  if(!handle){error.clear();return true;}
  PlatformOperation operation(execution_effect(ExecutionEffect::FlashWrite));
  if(!operation.accepted()){error="storage_execution_denied";return false;}
  std::lock_guard<std::recursive_mutex> lock(state.mutex);auto *slot=find_write(handle);
  if(!slot)return littlefs_internal_fail(&error,"storage_write_handle_stale");
  auto &write=*slot->transaction;
  if(write.file){(void)std::fclose(write.file);write.file=nullptr;}
  if(!write.path.empty()&&std::remove(write.path.c_str())!=0&&errno!=ENOENT)return littlefs_internal_fail(&error,"storage_write_cleanup_failed");
  write.path.clear();slot->release();handle={};littlefs_internal_refresh_usage();error.clear();return true;
}

// Called after mount and before accepting clients. Only these eight reserved
// transaction files belong to Storage; never enumerate or delete user paths.
bool littlefs_internal_cleanup_writes() {
  lunaredu::lunaros::PlatformOperation operation(lunaredu::lunaros::execution_effect(lunaredu::lunaros::ExecutionEffect::FlashWrite));
  if (!operation.accepted()) return false;
  for (size_t slot = 0; slot < state.write_slots.size(); ++slot) {
    if (state.write_slots[slot]) return littlefs_internal_fail(nullptr, "storage_stream_active");
    if (std::remove(transaction_path(slot).c_str()) != 0 && errno != ENOENT)
      return littlefs_internal_fail(nullptr, "transaction_cleanup_failed");
  }
  return true;
}

bool lunaredu::lunaros::storage_write_text_file(const char *path, const char *content,
                                     size_t *written_bytes) {
  const char *body = content ? content : "";
  return lunaredu::lunaros::storage_write_file(path, reinterpret_cast<const uint8_t *>(body),
                                   std::strlen(body), written_bytes);
}

bool lunaredu::lunaros::storage_write_file(const char *path, const uint8_t *data,
                              size_t size, size_t *written_bytes) {
  if (size && !data) {
    if (written_bytes) *written_bytes = 0;
    return false;
  }
  struct Buffer { const uint8_t *data; size_t size; } buffer{data, size};
  return lunaredu::lunaros::storage_write_sized_streamed_file(path,size,
      [](StorageFileChunkWriter writer, void *target, void *context, std::string &) {
        const auto &buffer = *static_cast<Buffer *>(context);
        return writer(buffer.data, buffer.size, target);
      }, &buffer, written_bytes, nullptr);
}

namespace {
bool write_streamed_file(
    const char *path, StorageWriteStreamProducer producer, void *producer_context,
    size_t *written_bytes, std::string *error,std::optional<size_t> expected) {
  lunaredu::lunaros::PlatformOperation operation(lunaredu::lunaros::execution_effect(lunaredu::lunaros::ExecutionEffect::FlashWrite));
  if (!operation.accepted()) return false;
  std::unique_lock<std::recursive_mutex> lock(state.mutex);
  if (written_bytes) *written_bytes = 0;
  if (!state.ready) return littlefs_internal_fail(error, "storage_not_ready");
  if (!path || path[0] != '/' || !lunaredu::lunaros::storage_is_safe_path(path) || !producer)
    return littlefs_internal_fail(error, "invalid_path");
  const auto normalized = littlefs_internal_normalize_path(path);
  if (normalized.empty() || normalized == "/") return littlefs_internal_fail(error, "invalid_path");
  LittlefsStreamLease lease;
  if (!lease.acquire(normalized, true, error)) return false;
  if (!ensure_parent_directory(normalized))
    return littlefs_internal_fail(error, "create_parent_directory_failed");

  WriteTransaction write;
  if (!write.open()) return littlefs_internal_fail(error, "open_transaction_failed");
  if (lunaredu::lunaros::storage_pool_supported_size(state.total_bytes) &&
      (lunaredu::lunaros::storage_pool_managed_path(normalized) ||
       normalized.rfind("/resources/", 0) == 0 || normalized.rfind("/cache/", 0) == 0)) {
    const auto used = lunaredu::lunaros::storage_pool_user_used();
    const auto reserve = lunaredu::lunaros::storage_pool_user_reserve();
    write.user_reserve = used < reserve ? reserve - used : 0;
  }
  if(expected&&!write.reserve(*expected))return littlefs_internal_fail(error,"storage_write_no_space");
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

} // namespace
bool lunaredu::lunaros::storage_write_streamed_file(
    const char *path,StorageWriteStreamProducer producer,void *context,
    size_t *written,std::string *error) {
  return write_streamed_file(path,producer,context,written,error,std::nullopt);
}
bool lunaredu::lunaros::storage_write_sized_streamed_file(
    const char *path,size_t expected,StorageWriteStreamProducer producer,void *context,
    size_t *written,std::string *error) {
  return write_streamed_file(path,producer,context,written,error,expected);
}

bool lunaredu::lunaros::storage_delete_file(const char *path) {
  lunaredu::lunaros::PlatformOperation operation(lunaredu::lunaros::execution_effect(lunaredu::lunaros::ExecutionEffect::FlashWrite));
  if (!operation.accepted()) return false;
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  if (!state.ready || path == nullptr || path[0] != '/' ||
      !lunaredu::lunaros::storage_is_safe_path(path) || !lunaredu::lunaros::storage_path_exists(path)) {
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
  // Newlib remove() delegates only to unlink(); LittleFS refuses directories
  // there. Select the non-recursive directory primitive explicitly, preserving
  // busy-path checks and rejection of nonempty folders.
  struct stat info{};
  if (::stat(host_path.c_str(), &info) != 0) return false;
  bool ok;
  if (S_ISDIR(info.st_mode)) {
#ifdef _WIN32
    ok = RemoveDirectoryA(host_path.c_str()) != 0;
#else
    ok = ::rmdir(host_path.c_str()) == 0;
#endif
  } else {
    ok = remove(host_path.c_str()) == 0;
  }
  if (ok) {
    littlefs_internal_refresh_usage();
  }
  return ok;
}

bool lunaredu::lunaros::storage_move_file(const char *source_path,
                              const char *destination_path,
                              std::string *error) {
  lunaredu::lunaros::PlatformOperation operation(lunaredu::lunaros::execution_effect(lunaredu::lunaros::ExecutionEffect::FlashWrite));
  if (!operation.accepted()) return false;
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  if (!state.ready || source_path == nullptr || destination_path == nullptr ||
      source_path[0] != '/' || destination_path[0] != '/' ||
      !lunaredu::lunaros::storage_is_safe_path(source_path) ||
      !lunaredu::lunaros::storage_is_safe_path(destination_path)) {
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
      !lunaredu::lunaros::storage_path_exists(source.c_str())) {
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
