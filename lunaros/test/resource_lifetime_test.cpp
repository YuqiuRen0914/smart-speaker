#include "../src/delivery/resource/resource_download_job.cpp"
#include "../src/delivery/resource/resource_storage.cpp"
#include <cassert>
#include <vector>

using namespace lunaredu::lunaros;
using namespace lunaredu::lunaros::internal;
namespace {
ResourceDeliveryRegistry::Entries records{};
ResourceRuntimeState runtime(records);
ResourceEntry *current = nullptr;
bool fail_download = false;
bool change_during_read = false;
std::vector<StorageListEntry> files;
size_t used_bytes = 0;
size_t undeletable_count = 40;
}

namespace lunaredu::lunaros::internal {
ResourceRuntimeState &resource_runtime_state() { return runtime; }
uint32_t resource_now_ms() { return 100; }
std::string private_resource_cache_root(const ResourceCacheScope &scope) {
  return scope.account_id.empty() ? std::string{} :
      std::string(kPrivateResourceRoot) + "/" + scope.account_id;
}
ResourceDownloadResult download_resource_transaction(
    const ResourceDescriptor &, const ResourceTransportConfig &,
    ResourceDownloadProgressCallback progress, const void *progress_context,
    ResourceDownloadCancellationCallback, const void *, const ResourceCacheScope &) {
  current->job = {22};
  current->descriptor.revision = "new";
  current->snapshot.state = ResourceState::Queued;
  current->snapshot.progress = 5;
  progress(99, progress_context);
  return {!fail_download, "/old-result", "hash", 4,
          fail_download ? "old_failure" : ""};
}
}

namespace lunaredu::lunaros {
bool storage_pool_enabled() { return false; } // This fixture covers legacy image caches.
bool storage_pool_admit(size_t, bool, std::string &) { return true; }
NetworkLinkState network_link_state() { return NetworkLinkState::Up; }
bool network_job_cancellation_requested(NetworkJobHandle) { return false; }
bool network_job_active(NetworkJobHandle) { return false; }
bool cancel_network_job(NetworkJobHandle, std::string &) { return true; }
bool submit_network_job(const NetworkJobDefinition &, NetworkJobHandle &,
                        std::string &) { return false; }
ResourceSnapshot resource_snapshot(const std::string &id) {
  auto *entry = runtime.registry.find(id);
  return entry == nullptr ? ResourceSnapshot{} : entry->snapshot;
}
bool storage_refresh_usage() { return true; }
size_t storage_total_bytes() { return 1024 * 1024; }
size_t storage_used_bytes() { return used_bytes; }
bool storage_delete_file(const char *path) {
  for (auto it = files.begin(); it != files.end(); ++it) {
    if (it->path == path) {
      if (it->modified_ms <= undeletable_count) return false;
      used_bytes -= it->size_bytes;
      files.erase(it);
      return true;
    }
  }
  return true;
}
bool storage_list_entries_recursive(const char *, uint8_t,
                                    StorageEntryVisitor visitor, void *ctx) {
  for (const auto &file : files) if (!visitor(file, ctx)) break;
  return true;
}
bool storage_list_entries(const char *, StorageEntryVisitor, void *) { return true; }
bool storage_read_file(const char *, std::vector<uint8_t> &) {
  if (change_during_read) {
    current->snapshot.revision = "newer-read";
    current->snapshot.path = "/newer-read";
  }
  return false;
}
bool storage_stream_file(const char *, StorageFileChunkWriter, void *, std::string *) {
  return false;
}
}

int main() {
  runtime.started.store(true);
  current = runtime.registry.find_or_allocate("test");
  current->job = {22};
  current->release_pending = true;
  assert(!finish_resource_job(current, {11}, true, "old").succeeded);
  assert(runtime.registry.find("test") == current && current->job.id == 22);
  current->release_pending = false;
  current->snapshot.state = ResourceState::Queued;
  update_resource_error("test", {11}, "late error");
  assert(current->snapshot.state == ResourceState::Queued);
  for (bool fail : {false, true}) {
    fail_download = fail;
    current->job = {11};
    current->descriptor.revision = "old";
    current->snapshot.error.clear();
    (void)download_resource("test", {11});
    assert(current->job.id == 22 && current->snapshot.state == ResourceState::Queued);
    assert(current->snapshot.progress == 5 && current->snapshot.error.empty());
  }

  current->snapshot.id = "test";
  current->snapshot.state = ResourceState::Ready;
  current->snapshot.revision = "cached";
  current->snapshot.path = "/corrupt";
  current->snapshot.size_bytes = 4;
  std::vector<uint8_t> content;
  assert(!read_ready_resource("test", content));
  assert(current->cache_invalid && current->snapshot.state == ResourceState::Error);
  assert(current->retry_after_ms == 1100);
  current->snapshot.state = ResourceState::Ready;
  current->cache_invalid = false;
  change_during_read = true;
  assert(!read_ready_resource("test", content));
  assert(current->snapshot.state == ResourceState::Ready && !current->cache_invalid);

  // More than six batches, including an entire undeletable oldest batch.
  for (size_t n = 1; n <= 200; ++n) {
    files.push_back({"/resources/private/" + std::to_string(n), 4096, false, n});
    used_bytes += 4096;
  }
  current->snapshot.path = "/resources/private/200";
  std::string error;
  assert(!ensure_private_resource_capacity(800 * 1024, error));
  assert(files.size() == 41); // 40 busy files + one live protected resource.
  assert(files.back().path == current->snapshot.path);
  undeletable_count = 0;
  assert(ensure_private_resource_capacity(800 * 1024, error));
  assert(files.back().path == current->snapshot.path);
  remove_private_resource_files();
  assert(files.empty() && used_bytes == 0);
}
