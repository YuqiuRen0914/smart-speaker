#include "lunaros_resource_service_id.h"
#include "lunaros_download_service_id.h"
#include "lunaros_network_service_id.h"
#include "lunaros_storage_service_id.h"
#include "lunaros_resource.h"
#include "platform/system.h"
#ifdef ESP_PLATFORM
#include <esp_attr.h>
#endif

#include <atomic>
#include <new>
#include <utility>

#include <esp_log.h>
#include "lunaros_network_jobs.h"
#include "kernel/service_executor.h"
#include "kernel/service.h"
#include "lunaros_storage.h"
#include "lunaros_resource_managed_assets.h"
#include "resource_download.h"
#include "managed_asset_compression.h"
#include "resource_runtime_internal.h"

namespace lunaredu::lunaros {
namespace {

constexpr size_t kResourceAccountIdMax = 256;
constexpr const char *kTag = "resource";
constexpr uint32_t kRequestFailureLogIntervalMs = 5000;
std::atomic<uint32_t> g_last_request_failure_log_ms{0};
std::atomic<bool> g_managed_startup_recovery{true};

void log_request_rejection(const ResourceDescriptor &descriptor,
                           const char *reason,
                           const std::string &detail = {}) {
  const uint32_t now = internal::resource_now_ms();
  const uint32_t previous = g_last_request_failure_log_ms.load();
  if (previous != 0 && now - previous < kRequestFailureLogIntervalMs) {
    return;
  }
  uint32_t expected = previous;
  if (!g_last_request_failure_log_ms.compare_exchange_strong(expected, now)) {
    return;
  }
  ESP_LOGW(kTag, "resource request rejected id=%s reason=%s%s%s",
       descriptor.id.c_str(), reason,
       detail.empty() ? "" : " detail=", detail.empty() ? "" : detail.c_str());
}

// The identity service owns durable authority. Boot never restores a private
// principal or deletes its cache before that owner has confirmed the scope.
bool setup_resource_runtime() {
  auto &runtime = internal::resource_runtime_state();
  {
    std::lock_guard<std::mutex> scope_lock(runtime.scope_mutex);
    if (runtime.started.load()) return true;
    if (!storage_ready()) return false;
    for (const char *path : {internal::kResourceCacheRoot,
                            internal::kPublicResourceRoot,
                            internal::kPrivateResourceRoot}) {
      if (!storage_ensure_directory(path)) {
        ESP_LOGE(kTag, "resource directory preparation failed: %s", path);
        return false;
      }
    }
    // Managed-asset restoration can submit requests and must see a ready
    // public cache. Every required directory exists before this publication.
    runtime.started.store(true);
    runtime.managed_restore_pending = true;
    runtime.managed_restore_attempted = false;
  }
  poll_managed_asset_recovery();
  return true;
}

// Caller holds both scope_mutex and mutex, in that order.
void suspend_private_scope_locked(internal::ResourceRuntimeState &runtime) {
  runtime.scope_valid = false;
  ++runtime.scope_generation;
  if (runtime.cleanup_job) {
    std::string ignored;
    (void)cancel_network_job(runtime.cleanup_job, ignored);
    runtime.cleanup_job = {};
  }
  for (internal::ResourceEntry &entry : runtime.registry.entries()) {
    if (!entry.occupied || entry.descriptor.scope == ResourceScope::Public) continue;
    if (entry.job) {
      std::string ignored;
      (void)cancel_network_job(entry.job, ignored);
    }
    entry.job = {};
    entry.release_pending = false;
    entry.descriptor.revision.clear();
    entry.snapshot = {};
  }
}

ServiceStartResult start_resource_service(void *) {
  return setup_resource_runtime()
             ? ServiceStartResult{ServiceStartDisposition::Ready,
                                  "resource_cache_ready"}
             : ServiceStartResult{ServiceStartDisposition::Failed,
                                  "resource_cache_initialization_failed"};
}

bool stop_resource_service(void *, std::string &error) {
  internal::ResourceRuntimeState &runtime = internal::resource_runtime_state();
  std::lock_guard<std::mutex> scope_lock(runtime.scope_mutex);
  runtime.started.store(false);
  internal::retire_managed_asset_decoder();
  std::lock_guard<std::mutex> lock(runtime.mutex);
  runtime.scope_valid = false;
  ++runtime.scope_generation;
  if (runtime.cleanup_job) {
    std::string ignored;
    (void)cancel_network_job(runtime.cleanup_job, ignored);
    runtime.cleanup_job = {};
  }
  for (internal::ResourceEntry &entry : runtime.registry.entries()) {
    if (!entry.occupied) {
      continue;
    }
    std::string ignored;
    if (entry.job) (void)cancel_network_job(entry.job, ignored);
    entry.job = {};
    entry.release_pending = false;
    entry.snapshot = {};
  }
  error.clear();
  return true;
}

} // namespace

void configure_managed_asset_startup_recovery(bool enabled) {
  g_managed_startup_recovery.store(enabled);
}

void poll_managed_asset_recovery() {
  auto &runtime = internal::resource_runtime_state();
  if (!g_managed_startup_recovery.load() || !runtime.started.load() ||
      !runtime.managed_restore_pending ||
      !managed_asset_updates_admitted()) return;
  const uint32_t now = internal::resource_now_ms();
  if (runtime.managed_restore_attempted &&
      now - runtime.managed_restore_attempt_ms < 5000) return;
  runtime.managed_restore_attempted = true;
  runtime.managed_restore_attempt_ms = now;
  runtime.managed_restore_pending = !resume_managed_assets();
}

ServiceNode resource_service_node() {
  static constexpr ServiceDependency kDependencies[] = {
      {kStorageServiceId, true},
      {kDownloadServiceId, true},
      {kNetworkRuntimeServiceId, false},
  };
  return {
      {kResourceServiceId, "Resource Delivery", kMainServiceExecutor,
       ServiceCriticality::Optional, kDependencies,
       sizeof(kDependencies) / sizeof(kDependencies[0])},
      start_resource_service,
      stop_resource_service,
  };
}

void configure_resource_transport(ResourceTransportConfig config) {
  internal::ResourceRuntimeState &runtime = internal::resource_runtime_state();
  std::lock_guard<std::mutex> lock(runtime.mutex);
  runtime.transport = std::move(config);
}

void suspend_private_resource_scope() {
  auto &runtime = internal::resource_runtime_state();
  std::lock_guard<std::mutex> scope_lock(runtime.scope_mutex);
  std::lock_guard<std::mutex> lock(runtime.mutex);
  suspend_private_scope_locked(runtime);
}

bool set_private_resource_scope(uint32_t binding_epoch,
                                const std::string &account_id,
                                std::string &error) {
  auto &runtime = internal::resource_runtime_state();
  std::lock_guard<std::mutex> scope_lock(runtime.scope_mutex);
  std::lock_guard<std::mutex> lock(runtime.mutex);
  error.clear();
  if (runtime.scope_valid && runtime.binding_epoch == binding_epoch &&
      runtime.account_id == account_id) return true;
  suspend_private_scope_locked(runtime);
  if (account_id.size() > kResourceAccountIdMax || account_id.find('\0') != std::string::npos) {
    error = "invalid_resource_account";
    return false;
  }
  runtime.binding_epoch = binding_epoch;
  runtime.account_id = account_id;
  runtime.scope_valid = true;
  runtime.cleanup_pending = true;
  internal::queue_private_resource_cleanup_locked();
  return true;
}

bool request_resource(ResourceDescriptor descriptor, int priority) {
  internal::ResourceRuntimeState &runtime = internal::resource_runtime_state();
  if (!runtime.started.load()) {
    log_request_rejection(descriptor, "runtime_not_started");
    return false;
  }
  if (!internal::valid_resource_descriptor(descriptor)) {
    log_request_rejection(descriptor, "invalid_descriptor");
    return false;
  }

  std::unique_lock<std::mutex> lock(runtime.mutex);
  internal::queue_private_resource_cleanup_locked();
  const internal::ResourceCacheScope cache_scope{
      runtime.binding_epoch, runtime.account_id};
  if (descriptor.scope != ResourceScope::Public &&
      (!runtime.scope_valid || cache_scope.account_id.empty())) return false;
  const auto already_handled = [&](const internal::ResourceEntry *entry) {
    return entry != nullptr && !entry->release_pending &&
        entry->descriptor.scope == descriptor.scope &&
        entry->descriptor.format == descriptor.format &&
        entry->descriptor.width == descriptor.width &&
        entry->descriptor.height == descriptor.height &&
        entry->descriptor.sha256 == descriptor.sha256 &&
        internal::resource_request_already_handled(
        entry->descriptor.revision, entry->snapshot.state,
        entry->retry_after_ms, descriptor, internal::resource_now_ms());
  };
  if (already_handled(runtime.registry.find(descriptor.id))) return true;
  const uint32_t scope_generation = runtime.scope_generation;
  lock.unlock();
  const std::string existing_path =
      internal::resource_cache_path(descriptor, cache_scope);
  if (existing_path.empty()) return false;
  const size_t expected_size = internal::resource_cached_size_if_valid(descriptor);
  const bool cached = expected_size > 0 &&
                      storage_path_exists(existing_path.c_str());
  lock.lock();
  if (!runtime.started.load() || runtime.scope_generation != scope_generation) {
    return false;
  }
  // A download or another request may have completed while checking storage.
  if (already_handled(runtime.registry.find(descriptor.id))) return true;
  internal::ResourceEntry *entry_ptr =
      runtime.registry.find_or_allocate(descriptor.id);
  if (entry_ptr == nullptr) {
    log_request_rejection(descriptor, "registry_full");
    return false;
  }
  internal::ResourceEntry &entry = *entry_ptr;
  if (entry.release_pending) return false;

  // This must also run on a cache hit: accepting a new publication detaches
  // the previous runner before any of its progress/error callbacks can return.
  if (entry.job) {
    std::string ignored;
    (void)cancel_network_job(entry.job, ignored);
    entry.job = {};
  }

  const bool same_revision = entry.descriptor.revision == descriptor.revision;
  if (!same_revision) {
    entry.cache_invalid = false;
    entry.failure_count = 0;
    entry.retry_after_ms = 0;
  }
  entry.descriptor = std::move(descriptor);
  entry.priority = priority;
  entry.snapshot = {};
  entry.snapshot.state = ResourceState::Queued;
  entry.snapshot.id = entry.descriptor.id;
  entry.snapshot.revision = entry.descriptor.revision;
  entry.snapshot.format = entry.descriptor.format;
  entry.snapshot.width = entry.descriptor.width;
  entry.snapshot.height = entry.descriptor.height;
  entry.snapshot.generation = runtime.next_resource_generation++;

  if (cached && !entry.cache_invalid) {
    entry.snapshot.state = ResourceState::Ready;
    entry.snapshot.path = existing_path;
    entry.snapshot.size_bytes = expected_size;
    entry.snapshot.progress = 100;
    return true;
  }

  std::string error;
  const bool queued = internal::queue_resource_download_locked(entry, error);
  if (!queued) {
    log_request_rejection(entry.descriptor, "queue_failed", error);
  }
  return queued;
}

bool release_resource(const std::string &id, bool delete_cached) {
  internal::ResourceRuntimeState &runtime = internal::resource_runtime_state();
  std::string cached_path;
  bool released = false;
  {
    std::lock_guard<std::mutex> lock(runtime.mutex);
    internal::ResourceEntry *entry = runtime.registry.find(id);
    if (entry == nullptr) return false;
    cached_path = entry->snapshot.path;
    if (!entry->job) {
      released = runtime.registry.erase(id);
    } else {
      const NetworkJobHandle handle = entry->job;
      entry->release_pending = true;
      // A running download compares its captured revision before installing.
      // Clearing it makes the result stale without invalidating the fixed slot.
      entry->descriptor.revision.clear();
      std::string ignored;
      (void)cancel_network_job(handle, ignored);
      const NetworkJobManagerSnapshot jobs = network_job_manager_snapshot();
      bool runner_still_owns_slot = false;
      for (size_t index = 0; index < jobs.record_count; ++index) {
        if (jobs.jobs[index].handle.id == handle.id) {
          runner_still_owns_slot = true;
          break;
        }
      }
      if (!runner_still_owns_slot) {
        released = runtime.registry.erase(id);
      }
    }
  }
  if (delete_cached && !cached_path.empty()) {
    (void)storage_delete_file(cached_path.c_str());
  }
  return released || !cached_path.empty();
}

ResourceSnapshot resource_snapshot(const std::string &id) {
  internal::ResourceRuntimeState &runtime = internal::resource_runtime_state();
  std::lock_guard<std::mutex> lock(runtime.mutex);
  const internal::ResourceEntry *entry = runtime.registry.find(id);
  return entry == nullptr ? ResourceSnapshot{} : entry->snapshot;
}

bool resource_publication_current(const std::string &id, uint64_t generation) {
  auto &runtime = internal::resource_runtime_state();
  std::lock_guard<std::mutex> lock(runtime.mutex);
  const auto *entry = runtime.registry.find(id);
  return runtime.started.load() && entry != nullptr &&
      entry->snapshot.state == ResourceState::Ready &&
      entry->snapshot.generation == generation;
}

void retry_resource(const std::string &id) {
  internal::ResourceRuntimeState &runtime = internal::resource_runtime_state();
  std::lock_guard<std::mutex> lock(runtime.mutex);
  internal::ResourceEntry *entry = runtime.registry.find(id);
  if (entry == nullptr || entry->snapshot.state != ResourceState::Error) {
    return;
  }
  internal::ResourceEntry &resource = *entry;
  resource.snapshot.state = ResourceState::Queued;
  resource.snapshot.error.clear();
  resource.snapshot.generation = runtime.next_resource_generation++;
  resource.failure_count = 0;
  resource.retry_after_ms = 0;
  std::string error;
  (void)internal::queue_resource_download_locked(resource, error);
}

} // namespace lunaredu::lunaros


namespace lunaredu::lunaros::internal {

ResourceRuntimeState &resource_runtime_state() {
  // A BSS section can contain only zero-initialized storage, not constexpr
  // std::strings whose empty value contains a pointer to their inline buffer.
  // Construct records once after PSRAM is ready; their addresses never move.
#ifdef ESP_PLATFORM
  alignas(ResourceDeliveryRegistry::Entries) static EXT_RAM_BSS_ATTR
      unsigned char storage[sizeof(ResourceDeliveryRegistry::Entries)];
#else
  alignas(ResourceDeliveryRegistry::Entries) static
      unsigned char storage[sizeof(ResourceDeliveryRegistry::Entries)];
#endif
  static auto &entries = *new (storage) ResourceDeliveryRegistry::Entries{};
  static ResourceRuntimeState state(entries);
  return state;
}

uint32_t resource_now_ms() {
  return system_monotonic_ms();
}

} // namespace lunaredu::lunaros::internal
