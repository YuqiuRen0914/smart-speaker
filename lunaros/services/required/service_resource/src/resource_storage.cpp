#include "lunaros_network_jobs.h"
#include "resource_runtime_internal.h"
#include "resource_storage_internal.h"
#include "resource_eviction_batch.h"

#include <algorithm>
#include <vector>

#include "lunaros_storage.h"
#include "lunaros_storage_pool.h"
#include "resource_download.h"

namespace lunaredu::lunaros {
namespace {

struct BufferReadContext {
  uint8_t *content = nullptr;
  size_t capacity = 0;
  size_t size = 0;
};

bool copy_resource_chunk(const uint8_t *data, size_t size, void *context) {
  auto *target = static_cast<BufferReadContext *>(context);
  if (target == nullptr || target->content == nullptr ||
      target->size > target->capacity ||
      size > target->capacity - target->size) {
    return false;
  }
  std::copy_n(data, size, target->content + target->size);
  target->size += size;
  return true;
}

void invalidate_failed_cached_read(const ResourceSnapshot &snapshot) {
  auto &runtime = internal::resource_runtime_state();
  std::lock_guard<std::mutex> lock(runtime.mutex);
  auto *entry = runtime.registry.find(snapshot.id);
  if (entry != nullptr && entry->snapshot.state == ResourceState::Ready &&
      entry->snapshot.generation == snapshot.generation &&
      entry->snapshot.revision == snapshot.revision &&
      entry->snapshot.path == snapshot.path) {
    entry->snapshot.state = ResourceState::Error;
    entry->cache_invalid = true;
    entry->snapshot.error = "resource_cache_read_failed";
    entry->retry_after_ms = internal::resource_now_ms() + 1000;
  }
}

bool valid_snapshot_size(const ResourceSnapshot &snapshot) {
  if (snapshot.format != "rgb666-u8x3" &&
      snapshot.format != "a8rgb666-u8x4") return snapshot.size_bytes != 0;
  const uint64_t expected = static_cast<uint64_t>(snapshot.width) *
      snapshot.height * (snapshot.format == "rgb666-u8x3" ? 3U : 4U);
  return expected != 0 && expected == snapshot.size_bytes;
}

bool publication_still_current(const ResourceSnapshot &snapshot) {
  auto &runtime = internal::resource_runtime_state();
  std::lock_guard<std::mutex> lock(runtime.mutex);
  auto *entry = runtime.registry.find(snapshot.id);
  return runtime.started.load() && entry != nullptr &&
      entry->snapshot.state == ResourceState::Ready &&
      entry->snapshot.generation == snapshot.generation &&
      entry->snapshot.path == snapshot.path;
}

} // namespace

bool read_ready_resource(const std::string &id, std::vector<uint8_t> &content,
                         ResourceSnapshot *snapshot) {
  const ResourceSnapshot current = resource_snapshot(id);
  if (current.state != ResourceState::Ready || current.path.empty()) {
    return false;
  }
  if (!valid_snapshot_size(current)) {
    invalidate_failed_cached_read(current);
    return false;
  }
  if (!storage_read_file(current.path.c_str(), content) ||
      content.size() != current.size_bytes) {
    invalidate_failed_cached_read(current);
    return false;
  }
  if (!publication_still_current(current)) {
    content.clear();
    return false;
  }
  if (snapshot != nullptr) {
    *snapshot = current;
  }
  return true;
}

bool read_ready_resource_into(const std::string &id, uint8_t *content,
                              size_t capacity, size_t *content_size,
                              ResourceSnapshot *snapshot) {
  if (content_size != nullptr) {
    *content_size = 0;
  }
  const ResourceSnapshot current = resource_snapshot(id);
  if (current.state != ResourceState::Ready || current.path.empty() ||
      content == nullptr) {
    return false;
  }
  if (!valid_snapshot_size(current)) {
    invalidate_failed_cached_read(current);
    return false;
  }
  // A caller's undersized buffer is not evidence of a corrupt file.
  if (current.size_bytes > capacity) return false;
  BufferReadContext context{content, capacity, 0};
  std::string error;
  if (!storage_stream_file(current.path.c_str(), copy_resource_chunk, &context,
                           &error) ||
      context.size != current.size_bytes) {
    invalidate_failed_cached_read(current);
    return false;
  }
  if (!publication_still_current(current)) return false;
  if (content_size != nullptr) {
    *content_size = context.size;
  }
  if (snapshot != nullptr) {
    *snapshot = current;
  }
  return true;
}

bool schedule_resource_read(ResourceReadOperation &read) {
  if (read.state.load(std::memory_order_acquire) != ResourceReadState::Idle ||
      read.content == nullptr || read.capacity == 0 || read.id.empty()) return false;
  const auto current = resource_snapshot(read.id);
  if (current.state != ResourceState::Ready || current.revision != read.revision ||
      current.size_bytes > read.capacity) return false;
  read.size = 0;
  read.generation = 0;
  read.publication_ = current.generation;
  read.state.store(ResourceReadState::Pending, std::memory_order_release);
  NetworkJobHandle handle;
  std::string error;
  NetworkJobDefinition job;
  job.owner_service_id = "resource.read";
  job.kind = "cached_read";
  job.priority = kNetworkPriorityInteractive;
  job.runner = [](NetworkJobHandle handle, void *context) -> NetworkJobRunResult {
    auto &read = *static_cast<ResourceReadOperation *>(context);
    ResourceSnapshot snapshot;
    size_t size = 0;
    const bool ok = !network_job_cancellation_requested(handle) &&
        resource_publication_current(read.id, read.publication_) &&
        read_ready_resource_into(read.id, read.content, read.capacity,
                                 &size, &snapshot) &&
        snapshot.revision == read.revision &&
        snapshot.generation == read.publication_ &&
        !network_job_cancellation_requested(handle);
    read.size = ok ? size : 0;
    read.generation = ok ? snapshot.generation : 0;
    // Last access: the caller may recycle storage after observing this.
    read.state.store(ok ? ResourceReadState::Succeeded : ResourceReadState::Failed,
                     std::memory_order_release);
    return {ok, false, {}};
  };
  job.context = &read;
  const bool admitted = submit_network_job(job, handle, error);
  read.job_id_ = handle.id;
  if (!admitted) read.state.store(ResourceReadState::Idle, std::memory_order_release);
  return admitted;
}

ResourceReadState resource_read_state(ResourceReadOperation &read) {
  auto state = read.state.load(std::memory_order_acquire);
  if (state == ResourceReadState::Pending && !network_job_active({read.job_id_})) {
    // Queued cancellation/reset skips the runner. Do not overwrite a racing
    // completion: an inactive handle guarantees no future access by the job.
    read.state.compare_exchange_strong(state, ResourceReadState::Failed,
                                      std::memory_order_acq_rel);
    state = read.state.load(std::memory_order_acquire);
  }
  return state;
}

namespace internal {

bool ensure_private_resource_capacity(size_t required_bytes,
                                      std::string &error) {
  auto &runtime = resource_runtime_state();
  uint32_t generation = 0;
  {
    std::lock_guard<std::mutex> lock(runtime.mutex);
    if (!runtime.scope_valid) { error = "resource_scope_unavailable"; return false; }
    generation = runtime.scope_generation;
  }
  // Pool admission only accounts for capacity; eviction below mutates files.
  if (storage_pool_enabled()) return storage_pool_admit(required_bytes, true, error);
  constexpr size_t kFilesystemReserveBytes = 64U * 1024U;
  error.clear();
  if (!storage_refresh_usage()) {
    error = "resource_storage_usage_unavailable";
    return false;
  }
  const size_t total = storage_total_bytes();
  size_t used = storage_used_bytes();
  if (required_bytes > total || total - required_bytes < kFilesystemReserveBytes) {
    error = "resource_storage_capacity_exceeded";
    return false;
  }
  const size_t required_free = required_bytes + kFilesystemReserveBytes;
  if (used <= total && total - used >= required_free) return true;

  std::array<std::string, kResourceRegistryCapacity> protected_paths{};
  size_t protected_count = 0;
  {
    std::lock_guard<std::mutex> lock(runtime.mutex);
    for (const ResourceEntry &entry : runtime.registry.entries()) {
      if (entry.occupied && !entry.snapshot.path.empty()) {
        protected_paths[protected_count++] = entry.snapshot.path;
      }
    }
  }

  StorageListEntry cursor;
  for (;;) {
    ResourceEvictionBatch candidates;
    struct Collection {
      ResourceEvictionBatch &batch;
      const std::array<std::string, kResourceRegistryCapacity> &paths;
      size_t protected_count;
      const StorageListEntry &cursor;
    } collection{candidates, protected_paths, protected_count, cursor};
    auto collect = [](const StorageListEntry &entry, void *opaque) {
      auto &c = *static_cast<Collection *>(opaque);
      if ((!c.cursor.path.empty() && !resource_file_older(c.cursor, entry)) ||
          std::find(c.paths.begin(),
                    c.paths.begin() + c.protected_count, entry.path) !=
              c.paths.begin() + c.protected_count) return true;
      c.batch.offer(entry);
      return true;
    };
    storage_list_entries_recursive(kPrivateResourceRoot, 3, collect, &collection);
    if (candidates.count == 0) break;
    candidates.sort();
    for (size_t n = 0; n < candidates.count; ++n) {
      const auto &candidate = candidates.entries[n];
      std::lock_guard<std::mutex> scope_lock(runtime.scope_mutex);
      {
        std::lock_guard<std::mutex> lock(runtime.mutex);
        if (!runtime.scope_valid || runtime.scope_generation != generation) {
          error = "resource_scope_changed"; return false;
        }
      }
      if (!storage_delete_file(candidate.path.c_str())) continue;
      used = candidate.size_bytes > used ? 0 : used - candidate.size_bytes;
      if (used <= total && total - used >= required_free) return true;
    }
    // Advance even if busy/corrupt files could not be removed.
    cursor = candidates.entries[candidates.count - 1];
  }
  if (storage_refresh_usage() && storage_used_bytes() <= total &&
      total - storage_used_bytes() >= required_free) {
    return true;
  }
  error = "resource_storage_capacity_exceeded";
  return false;
}

bool remove_private_resource_files() {
  auto &runtime = resource_runtime_state();
  ResourceCacheScope scope;
  uint32_t generation = 0;
  {
    std::lock_guard<std::mutex> lock(runtime.mutex);
    if (!runtime.scope_valid) return false;
    scope = {runtime.binding_epoch, runtime.account_id};
    generation = runtime.scope_generation;
  }
  const std::string current_root = private_resource_cache_root(scope);
  if (!scope.account_id.empty() && current_root.empty()) return false;
  const std::string protected_prefix = current_root.empty() ? std::string{} :
      current_root + "/";
  bool complete = true;
  bool scope_changed = false;
  const auto remove_retired = [&](const std::string &path) {
    // A transition only waits for one filesystem mutation, not an entire
    // multi-batch scan. Never acquire the registry mutex from a list visitor.
    std::lock_guard<std::mutex> scope_lock(runtime.scope_mutex);
    {
      std::lock_guard<std::mutex> lock(runtime.mutex);
      if (runtime.scope_generation != generation) {
        scope_changed = true;
        return false;
      }
    }
    return storage_delete_file(path.c_str());
  };
  StorageListEntry cursor;
  for (;;) {
    ResourceEvictionBatch batch;
    struct Collection {
      ResourceEvictionBatch &batch;
      const StorageListEntry &cursor;
      const std::string &protected_prefix;
    } collection{batch, cursor, protected_prefix};
    auto collect = [](const StorageListEntry &entry, void *opaque) {
      auto &c = *static_cast<Collection *>(opaque);
      if (!c.protected_prefix.empty() &&
          entry.path.compare(0, c.protected_prefix.size(), c.protected_prefix) == 0)
        return true;
      if (c.cursor.path.empty() || resource_file_older(c.cursor, entry))
        c.batch.offer(entry);
      return true;
    };
    if (!storage_list_entries_recursive(kPrivateResourceRoot, 3, collect,
                                         &collection)) complete = false;
    if (batch.count == 0) break;
    batch.sort();
    for (size_t n = 0; n < batch.count; ++n) {
      if (!remove_retired(batch.entries[n].path)) complete = false;
      if (scope_changed) return false;
    }
    cursor = batch.entries[batch.count - 1];
  }
  // Scope directories are small but not free; remove empty retired folders as
  // well. A busy child keeps its folder and the pending retry flag alive.
  cursor = {};
  for (;;) {
    ResourceEvictionBatch directories;
    struct DirectoryCollection {
      ResourceEvictionBatch &batch;
      const StorageListEntry &cursor;
      const std::string &current_root;
    } collection{directories, cursor, current_root};
    auto collect = [](const StorageListEntry &entry, void *opaque) {
      auto &c = *static_cast<DirectoryCollection *>(opaque);
      if (!entry.is_directory || entry.path == c.current_root ||
          (!c.cursor.path.empty() && !resource_file_older(c.cursor, entry))) return true;
      StorageListEntry directory = entry;
      directory.is_directory = false;
      c.batch.offer(directory);
      return true;
    };
    if (!storage_list_entries(kPrivateResourceRoot, collect, &collection)) complete = false;
    if (directories.count == 0) break;
    directories.sort();
    for (size_t n = 0; n < directories.count; ++n) {
      if (!remove_retired(directories.entries[n].path)) complete = false;
      if (scope_changed) return false;
    }
    cursor = directories.entries[directories.count - 1];
  }
  {
    std::lock_guard<std::mutex> scope_lock(runtime.scope_mutex);
    std::lock_guard<std::mutex> lock(runtime.mutex);
    if (runtime.scope_generation != generation) return false;
    runtime.cleanup_pending = !complete;
  }
  return complete;
}

namespace {
NetworkJobRunResult run_private_cleanup(NetworkJobHandle handle, void *) {
  auto &runtime = resource_runtime_state();
  {
    std::lock_guard<std::mutex> lock(runtime.mutex);
    if (!runtime.started.load() || runtime.cleanup_job.id != handle.id)
      return {false, false, "resource_cleanup_stale"};
  }
  (void)remove_private_resource_files();
  std::lock_guard<std::mutex> lock(runtime.mutex);
  if (runtime.cleanup_job.id != handle.id)
    return {false, false, "resource_cleanup_stale"};
  const bool complete = !runtime.cleanup_pending;
  const bool retry = !complete && ++runtime.cleanup_attempts < 255 &&
      !network_job_cancellation_requested(handle);
  if (!retry) runtime.cleanup_job = {};
  return {complete, retry, complete ? "" : "resource_cleanup_pending"};
}
} // namespace

void queue_private_resource_cleanup_locked() {
  auto &runtime = resource_runtime_state();
  if (!runtime.started.load() || !runtime.scope_valid || !runtime.cleanup_pending) return;
  // OTA can cancel a runner after it decided to retry but before the
  // scheduler installs that result. A retired handle must not block admission.
  if (runtime.cleanup_job && network_job_active(runtime.cleanup_job)) return;
  runtime.cleanup_job = {};
  runtime.cleanup_attempts = 0;
  std::string ignored;
  NetworkJobDefinition job;
  job.owner_service_id = "resource.cleanup";
  job.kind = "private_cache_cleanup";
  job.priority = kNetworkPriorityMaintenance;
  job.maximum_attempts = 255;
  job.runner = run_private_cleanup;
  job.retry_backoff_initial_ms = 1000;
  job.retry_backoff_maximum_ms = 30000;
  (void)submit_network_job(job, runtime.cleanup_job, ignored);
}

} // namespace internal
} // namespace lunaredu::lunaros
