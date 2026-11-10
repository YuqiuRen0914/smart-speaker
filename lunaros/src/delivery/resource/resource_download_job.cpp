#include "delivery/resource/resource_runtime_internal.h"

#include <algorithm>

#include <esp_heap_caps.h>

#include "lunaredu_lunaros/network/network.h"
#include "network/http/network_transport_policy.h"
#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/persistence/storage.h"
#include "delivery/resource/resource_download.h"

namespace lunaredu::lunaros::internal {
namespace {



void update_resource_error(const std::string &id, NetworkJobHandle handle,
                            const std::string &error) {
  ResourceRuntimeState &runtime = resource_runtime_state();
  std::lock_guard<std::mutex> lock(runtime.mutex);
  ResourceEntry *resource = runtime.registry.find(id);
  if (resource == nullptr || resource->release_pending ||
      resource->job.id != handle.id) {
    return;
  }
  resource->snapshot.state = ResourceState::Error;
  resource->snapshot.error = error;
  resource->snapshot.progress = 0;
  resource->failure_count = std::min<uint8_t>(
      static_cast<uint8_t>(resource->failure_count + 1), 6);
  resource->retry_after_ms =
      resource_now_ms() + resource_failure_backoff_ms(resource->failure_count);
  LOGW("resource", "download failed id=%s error=%s retry_after_ms=%u",
       id.c_str(), error.c_str(), static_cast<unsigned>(resource->retry_after_ms));
}

NetworkJobRunResult finish_resource_job(ResourceEntry *entry,
                                        NetworkJobHandle handle,
                                        bool succeeded,
                                        const std::string &detail) {
  ResourceRuntimeState &runtime = resource_runtime_state();
  std::lock_guard<std::mutex> lock(runtime.mutex);
  if (entry->job.id != handle.id) return {false, false, "resource_job_stale"};
  entry->job = {};
  if (entry->release_pending) {
    const std::string id = entry->descriptor.id;
    (void)runtime.registry.erase(id);
    return {false, false, "resource_released"};
  }
  return {succeeded, false, detail};
}

bool download_resource(const std::string &id, NetworkJobHandle handle) {
  struct ProgressContext {
    const std::string *id = nullptr;
    NetworkJobHandle handle;
    uint64_t generation = 0;
  };
  ResourceRuntimeState &runtime = resource_runtime_state();
  ResourceDescriptor descriptor;
  ResourceTransportConfig transport;
  ResourceCacheScope scope;
  uint64_t generation = 0;
  {
    std::lock_guard<std::mutex> lock(runtime.mutex);
    ResourceEntry *entry = runtime.registry.find(id);
    if (entry == nullptr || entry->job.id != handle.id || entry->release_pending) {
      return false;
    }
    entry->snapshot.state = ResourceState::Downloading;
    entry->snapshot.error.clear();
    descriptor = entry->descriptor;
    transport = runtime.transport;
    scope = {runtime.binding_epoch, runtime.account_id};
    generation = entry->snapshot.generation;
  }

  const auto progress_callback = [](int progress, const void *context) {
    const auto *progress_context =
        static_cast<const ProgressContext *>(context);
    if (progress_context == nullptr || progress_context->id == nullptr) {
      return;
    }
    ResourceRuntimeState &state = resource_runtime_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    ResourceEntry *entry = state.registry.find(*progress_context->id);
    if (entry != nullptr &&
        entry->job.id == progress_context->handle.id &&
        entry->snapshot.generation == progress_context->generation) {
      entry->snapshot.progress = progress;
    }
  };
  const auto cancellation_callback = [](const void *context) {
    const auto &job = *static_cast<const ProgressContext *>(context);
    if (network_job_cancellation_requested(job.handle)) return true;
    auto &state = resource_runtime_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    const auto *entry = state.registry.find(*job.id);
    return !state.started.load() || entry == nullptr || entry->release_pending ||
        entry->job.id != job.handle.id ||
        entry->snapshot.generation != job.generation;
  };
  const ProgressContext progress_context{&id, handle, generation};
  const ResourceDownloadResult result = download_resource_transaction(
      descriptor, transport, progress_callback, &progress_context,
      cancellation_callback, &progress_context, scope);
  if (result.error == "download_cancelled" ||
      cancellation_callback(&progress_context)) {
    std::lock_guard<std::mutex> lock(runtime.mutex);
    // A scope may change between the final cancellation check and rename.
    // Such output remains isolated; arrange another garbage collection pass.
    if (result.success && descriptor.scope != ResourceScope::Public) {
      runtime.cleanup_pending = true;
      queue_private_resource_cleanup_locked();
    }
    ResourceEntry *entry = runtime.registry.find(id);
    if (entry != nullptr && !entry->release_pending &&
        entry->job.id == handle.id) {
      // A future request for the same revision may queue again after OTA.
      // Do not count maintenance deferral as a failed download/backoff.
      entry->snapshot.state = ResourceState::Error;
      entry->snapshot.error = "download_deferred_for_ota";
      entry->snapshot.progress = 0;
      entry->retry_after_ms = 0;
    }
    return true;
  }
  if (!result.success) {
    update_resource_error(id, handle, result.error);
    return false;
  }

  bool still_current = false;
  {
    std::lock_guard<std::mutex> lock(runtime.mutex);
    ResourceEntry *entry = runtime.registry.find(id);
    still_current = entry != nullptr &&
                    entry->job.id == handle.id && !entry->release_pending &&
                    entry->descriptor.revision == descriptor.revision;
    if (still_current) {
      entry->cache_invalid = false;
      entry->snapshot.state = ResourceState::Ready;
      entry->snapshot.path = result.path;
      entry->snapshot.error.clear();
      entry->snapshot.sha256 = result.sha256;
      entry->snapshot.size_bytes = result.size_bytes;
      entry->snapshot.progress = 100;
      entry->failure_count = 0;
      entry->retry_after_ms = 0;
    }
  }
  // Do not unlink a stale result here: another publication may already be
  // using the same cache path. Retired scopes and capacity eviction reclaim it.
  return false;
}

NetworkJobRunResult run_resource_download(NetworkJobHandle handle,
                                          void *context) {
  auto *entry = static_cast<ResourceEntry *>(context);
  if (entry == nullptr) {
    return {false, false, "resource_context_missing"};
  }
  ResourceRuntimeState &runtime = resource_runtime_state();
  std::string id;
  bool allow_plaintext_development = false;
  {
    std::lock_guard<std::mutex> lock(runtime.mutex);
    if (entry->job.id != handle.id) {
      return {false, false, "resource_job_stale"};
    }
    if (entry->release_pending) {
      const std::string id = entry->descriptor.id;
      entry->job = {};
      (void)runtime.registry.erase(id);
      return {false, false, "resource_released"};
    }
    id = entry->descriptor.id;
    allow_plaintext_development = runtime.transport.allow_plaintext_development;
  }
  auto reject_with_backoff = [&](const char *reason) {
    update_resource_error(id, handle, reason);
    return finish_resource_job(entry, handle, false, reason);
  };
  // Resource delivery is background work. Do not suspend the assistant while
  // Wi-Fi is down or while the internal TLS scratch block is unavailable;
  // record a normal resource backoff instead and let the next request retry.
  if (network_link_state() != NetworkLinkState::Up) {
    return reject_with_backoff("network_not_connected");
  }
  // Development resource delivery is plain HTTP through the LAN gateway and
  // does not allocate the TLS handshake scratch area.  Applying the
  // production TLS threshold here made every avatar/pet download back off on
  // devices whose audio runtime leaves only a 4 KiB contiguous block.
  const bool tls_memory_unavailable =
      !network_tls_memory_available(!allow_plaintext_development,
          heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  if (tls_memory_unavailable) {
    return reject_with_backoff("cloud_tls_internal_memory_low");
  }
  // The admission check above proves that the TLS request can run without
  // reclaiming XiaoZhi's resident audio/MQTT memory. Resource delivery is
  // background work, so it must never enter the destructive maintenance gate:
  // doing so aborts an in-flight voice reply merely to fetch an avatar or pet
  // animation frame. If memory is insufficient we back off above; otherwise
  // download directly and preserve the assistant session.
  const bool cancelled_for_ota = download_resource(id, handle);
  if (cancelled_for_ota) {
    return finish_resource_job(entry, handle, false,
                               "download_deferred_for_ota");
  }
  bool succeeded = false;
  std::string detail;
  {
    std::lock_guard<std::mutex> lock(runtime.mutex);
    succeeded = !entry->release_pending &&
                entry->snapshot.state == ResourceState::Ready;
    detail = entry->snapshot.error;
  }
  return finish_resource_job(entry, handle, succeeded, detail);
}

} // namespace

bool queue_resource_download_locked(ResourceEntry &entry,
                                    std::string &error) {
  if (entry.job) {
    std::string ignored;
    (void)cancel_network_job(entry.job, ignored);
    entry.job = {};
  }
  if (!submit_network_job(
          {"resource.delivery", "download",
           static_cast<uint8_t>(kNetworkPriorityBackground +
                                std::clamp(entry.priority, 0, 31)),
           1, 0, run_resource_download, &entry, false},
          entry.job, error)) {
    entry.snapshot.state = ResourceState::Error;
    entry.snapshot.error = error;
    return false;
  }
  return true;
}

} // namespace lunaredu::lunaros::internal
