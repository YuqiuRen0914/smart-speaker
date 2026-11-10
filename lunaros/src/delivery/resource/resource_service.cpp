#include "lunaredu_lunaros/delivery/resources.h"

#include <cstdlib>
#include <atomic>
#include <utility>

#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/network/network_jobs.h"
#include "lunaredu_lunaros/kernel/service_executor.h"
#include "lunaredu_lunaros/kernel/service_host.h"
#include "lunaredu_lunaros/kernel/service_ids.h"
#include "lunaredu_lunaros/persistence/settings.h"
#include "lunaredu_lunaros/persistence/storage.h"
#include "lunaredu_lunaros/delivery/managed_assets.h"
#include "delivery/resource/resource_download.h"
#include "delivery/resource/resource_runtime_internal.h"

namespace lunaredu::lunaros {
namespace {

constexpr const char *kResourceSettingsNamespace = "lunar_resources";
constexpr const char *kResourceBindingEpochKey = "binding_epoch";
constexpr const char *kResourceAccountIdKey = "account_id";
constexpr const char *kTag = "resource";
constexpr uint32_t kRequestFailureLogIntervalMs = 5000;
std::atomic<uint32_t> g_last_request_failure_log_ms{0};

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
  LOGW(kTag, "resource request rejected id=%s reason=%s%s%s",
       descriptor.id.c_str(), reason,
       detail.empty() ? "" : " detail=", detail.empty() ? "" : detail.c_str());
}

uint32_t parse_binding_epoch(const std::string &value) {
  if (value.empty()) {
    return 0;
  }
  char *end = nullptr;
  const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0' || parsed > UINT32_MAX) {
    return 0;
  }
  return static_cast<uint32_t>(parsed);
}

bool setup_resource_runtime() {
  internal::ResourceRuntimeState &runtime = internal::resource_runtime_state();
  bool expected = false;
  if (!runtime.started.compare_exchange_strong(expected, true)) {
    return true;
  }
  // Storage is a declared service dependency. Resource setup must not mount
  // filesystems or repair composition order as a hidden side effect.
  if (!storage_ready()) {
    runtime.started.store(false);
    return false;
  }
  storage_ensure_directory(internal::kResourceCacheRoot);
  storage_ensure_directory(internal::kPublicResourceRoot);
  storage_ensure_directory(internal::kPrivateResourceRoot);
  resume_managed_assets();
  runtime.binding_epoch = parse_binding_epoch(settings_get_string(
      kResourceSettingsNamespace, kResourceBindingEpochKey));
  runtime.account_id = settings_get_string(kResourceSettingsNamespace,
                                           kResourceAccountIdKey);
  {
    std::lock_guard<std::mutex> lock(runtime.mutex);
    runtime.cleanup_pending = true;
    internal::queue_private_resource_cleanup_locked();
  }
  return true;
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
  runtime.started.store(false);
  std::lock_guard<std::mutex> lock(runtime.mutex);
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

ServiceNode resource_service_node() {
  static constexpr ServiceDependency kDependencies[] = {
      {kStorageServiceId, true},
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

void set_private_resource_scope(uint32_t binding_epoch,
                                const std::string &account_id) {
  internal::ResourceRuntimeState &runtime = internal::resource_runtime_state();
  std::lock_guard<std::mutex> scope_lock(runtime.scope_mutex);
  bool changed = false;
  {
    std::lock_guard<std::mutex> lock(runtime.mutex);
    changed = runtime.binding_epoch != binding_epoch ||
              runtime.account_id != account_id;
    if (!changed) {
      return;
    }
    runtime.binding_epoch = binding_epoch;
    runtime.account_id = account_id;
    ++runtime.scope_generation;
    for (internal::ResourceEntry &entry : runtime.registry.entries()) {
      if (!entry.occupied ||
          entry.descriptor.scope == ResourceScope::Public) {
        continue;
      }
      if (entry.job) {
        std::string ignored;
        (void)cancel_network_job(entry.job, ignored);
      }
      // Keep the map node stable until any in-flight netio runner returns;
      // changing the revision makes the old download fail its install check.
      entry.job = {};
      entry.release_pending = false;
      entry.descriptor.revision.clear();
      entry.snapshot = {};
    }
    runtime.cleanup_pending = true;
    internal::queue_private_resource_cleanup_locked();
  }
  if (changed) {
    (void)settings_set_string(kResourceSettingsNamespace,
                              kResourceBindingEpochKey,
                              std::to_string(binding_epoch));
    (void)settings_set_string(kResourceSettingsNamespace,
                              kResourceAccountIdKey, account_id);
  }
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
      cache_scope.account_id.empty()) return false;
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
