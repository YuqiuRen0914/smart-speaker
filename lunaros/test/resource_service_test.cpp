#include "../src/delivery/resource/resource_service.cpp"
#include "../src/delivery/resource/resource_download_job.cpp"
#include "../src/delivery/resource/resource_storage.cpp"
#include "network/network_job_manager_internal.h"
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <set>

using namespace lunaredu::lunaros;
using namespace lunaredu::lunaros::internal;
namespace {
ResourceDeliveryRegistry::Entries records{};
ResourceRuntimeState runtime(records);
std::map<std::string, std::string> files;
std::set<std::string> directories;
std::string busy_path;
std::function<void()> download_hook;
std::function<void()> read_hook;
std::function<void()> list_hook;
std::function<void()> delete_hook;
uint32_t clock_ms = 100;
bool fail_download = false;

ResourceDescriptor descriptor(const char *revision = "v1") {
  ResourceDescriptor result;
  result.id = "avatar"; result.revision = revision; result.url = "/avatar";
  result.format = "rgb666-u8x3"; result.width = 1; result.height = 1;
  result.maximum_bytes = 3; result.scope = ResourceScope::AccountPrivate;
  return result;
}
void reset() {
  network_job_manager_internal().reset();
  for (auto &entry : records) {
    if (entry.occupied) {
      const std::string id = entry.descriptor.id;
      runtime.registry.erase(id);
    }
  }
  runtime.started.store(true);
  runtime.account_id = "A"; runtime.binding_epoch = 1;
  runtime.cleanup_pending = false; runtime.cleanup_job = {};
  runtime.cleanup_attempts = 0;
  files.clear(); directories.clear(); busy_path.clear(); download_hook = {}; read_hook = {};
  list_hook = {}; delete_hook = {};
  fail_download = false; clock_ms += 10000;
}
void drain_jobs() {
  for (size_t n = 0; n < 32 && run_next_network_job(clock_ms); ++n) {}
}
}

namespace lunaredu::lunaros::internal {
ResourceRuntimeState &resource_runtime_state() { return runtime; }
uint32_t resource_now_ms() { return clock_ms; }
// Real cryptographic encoding/path limits are tested by resource_download_test.
std::string private_resource_cache_root(const ResourceCacheScope &scope) {
  return scope.account_id.empty() ? std::string{} :
      std::string(kPrivateResourceRoot) + "/" + scope.account_id +
      std::to_string(scope.binding_epoch);
}
std::string resource_cache_path(const ResourceDescriptor &d,
                                const ResourceCacheScope &scope) {
  return (d.scope == ResourceScope::Public ? std::string(kPublicResourceRoot) :
      private_resource_cache_root(scope)) + "/" + d.id + "." + d.revision;
}
ResourceDownloadResult download_resource_transaction(
    const ResourceDescriptor &d, const ResourceTransportConfig &,
    ResourceDownloadProgressCallback progress, const void *progress_context,
    ResourceDownloadCancellationCallback, const void *,
    const ResourceCacheScope &scope) {
  if (download_hook) { auto hook = std::move(download_hook); hook(); }
  progress(17, progress_context);
  if (fail_download) return {false, {}, {}, 0, "old_download_failed"};
  const std::string path = resource_cache_path(d, scope);
  files[path] = "abc";
  if (d.scope != ResourceScope::Public) directories.insert(private_resource_cache_root(scope));
  return {true, path, "hash", 3, {}};
}
}

namespace lunaredu::lunaros {
bool storage_pool_enabled() { return false; }
bool storage_pool_admit(size_t, bool, std::string &) { return true; }
// These cases exercise request/cache lifetimes, not startup asset publication.
void resume_managed_assets() {}
NetworkLinkState network_link_state() { return NetworkLinkState::Up; }
bool storage_ready() { return true; }
bool storage_ensure_directory(const char *) { return true; }
bool storage_path_exists(const char *path) { return files.count(path) != 0; }
bool storage_refresh_usage() { return true; }
size_t storage_total_bytes() { return 1024 * 1024; }
size_t storage_used_bytes() { return files.size() * 3; }
bool storage_delete_file(const char *path) {
  if (delete_hook) { auto hook = std::move(delete_hook); hook(); }
  if (path == busy_path) return false;
  if (directories.count(path) != 0) {
    const auto prefix = std::string(path) + "/";
    for (const auto &[name, bytes] : files) {
      (void)bytes;
      if (name.compare(0, prefix.size(), prefix) == 0) return false;
    }
    directories.erase(path);
  }
  files.erase(path); return true;
}
bool storage_list_entries_recursive(const char *, uint8_t,
                                    StorageEntryVisitor visitor, void *context) {
  for (const auto &[path, bytes] : files) {
    if (path.compare(0, std::string(kPrivateResourceRoot).size(),
                     kPrivateResourceRoot) == 0 &&
        !visitor({path, bytes.size(), false, 1}, context)) break;
  }
  if (list_hook) { auto hook = std::move(list_hook); hook(); }
  return true;
}
bool storage_list_entries(const char *, StorageEntryVisitor visitor, void *context) {
  for (const auto &path : directories)
    if (!visitor({path, 0, true, 1}, context)) break;
  return true;
}
bool storage_read_file(const char *path, std::vector<uint8_t> &content) {
  const auto it = files.find(path);
  if (it == files.end()) return false;
  content.assign(it->second.begin(), it->second.end());
  if (read_hook) { auto hook = std::move(read_hook); hook(); }
  return true;
}
bool storage_stream_file(const char *path, StorageFileChunkWriter writer,
                         void *context, std::string *) {
  const auto it = files.find(path);
  if (it == files.end()) return false;
  const auto bytes = it->second;
  if (read_hook) { auto hook = std::move(read_hook); hook(); }
  return writer(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size(), context);
}
std::string settings_get_string(const char *, const char *, const char *) { return {}; }
bool settings_set_string(const char *, const char *, const std::string &) { return true; }
}

int main() {
  // Exercise the actual request/runner/scheduler path, including cache hits.
  for (const bool failure : {false, true}) {
    reset(); fail_download = failure;
    const auto next = descriptor("v2");
    const auto path = resource_cache_path(next, {1, "A"});
    files[path] = "new";
    assert(request_resource(descriptor("v1")));
    download_hook = [&] { assert(request_resource(next)); };
    drain_jobs();
    const auto snapshot = resource_snapshot("avatar");
    assert(snapshot.state == ResourceState::Ready && snapshot.revision == "v2");
    assert(snapshot.progress == 100 && snapshot.error.empty());
    assert(files[path] == "new");
    assert(!runtime.registry.find("avatar")->job);
  }

  reset();
  const auto d = descriptor();
  const auto old_path = resource_cache_path(d, {1, "A"});
  const auto new_path = resource_cache_path(d, {1, "B"});
  files[old_path] = "old"; busy_path = old_path;
  directories.insert(private_resource_cache_root({1, "A"}));
  assert(request_resource(d));
  const auto old_generation = resource_snapshot(d.id).generation;
  set_private_resource_scope(1, "B");
  assert(request_resource(d));
  assert(resource_snapshot(d.id).state == ResourceState::Queued);
  drain_jobs();
  assert(resource_snapshot(d.id).path == new_path);
  assert(resource_snapshot(d.id).generation != old_generation);
  assert(files.count(old_path) == 1 && runtime.cleanup_pending);
  busy_path.clear(); clock_ms += 31000; drain_jobs();
  assert(files.count(old_path) == 0 && files.count(new_path) == 1);
  assert(!runtime.cleanup_pending && !runtime.cleanup_job);
  assert(directories.count(private_resource_cache_root({1, "A"})) == 0);
  assert(directories.count(private_resource_cache_root({1, "B"})) == 1);

  // A finished read from the previous publication must not be returned.
  uint8_t bytes[3]{}; size_t size = 99;
  read_hook = [] { set_private_resource_scope(2, "C"); };
  assert(!read_ready_resource_into(d.id, bytes, sizeof(bytes), &size));
  assert(size == 0);
  drain_jobs();

  // Switching scope after a transaction's last cancellation check cannot
  // install the old publication or leave its retired file without cleanup.
  reset(); assert(request_resource(d));
  download_hook = [] { set_private_resource_scope(1, "B"); };
  drain_jobs();
  assert(resource_snapshot(d.id).state != ResourceState::Ready);
  assert(files.count(old_path) == 0);

  reset(); files[old_path] = "a";
  assert(request_resource(d));
  assert(!read_ready_resource_into(d.id, bytes, sizeof(bytes), &size));
  assert(runtime.registry.find(d.id)->cache_invalid);
  clock_ms += 2000;
  assert(request_resource(d)); drain_jobs();
  assert(read_ready_resource_into(d.id, bytes, sizeof(bytes), &size));
  assert(size == 3 && bytes[0] == 'a');
  assert(!read_ready_resource_into(d.id, bytes, 1, &size));
  assert(resource_snapshot(d.id).state == ResourceState::Ready);

  // Historical successful downloads stored their own incorrect size.
  auto *entry = runtime.registry.find(d.id);
  entry->snapshot.size_bytes = 1;
  assert(!read_ready_resource_into(d.id, bytes, sizeof(bytes), &size));
  assert(resource_snapshot(d.id).state == ResourceState::Error && entry->cache_invalid);
  clock_ms += 2000; assert(request_resource(d)); drain_jobs();
  assert(read_ready_resource_into(d.id, bytes, sizeof(bytes), &size));

  const auto before_retry = resource_snapshot(d.id).generation;
  read_hook = [&] {
    entry->snapshot.state = ResourceState::Error;
    retry_resource(d.id); drain_jobs();
    assert(resource_snapshot(d.id).generation != before_retry);
  };
  assert(!read_ready_resource_into(d.id, bytes, sizeof(bytes), &size));
  assert(size == 0 && resource_snapshot(d.id).state == ResourceState::Ready);

  // Service stop invalidates cached publications too, not just active jobs.
  read_hook = [] { std::string error; assert(stop_resource_service(nullptr, error)); };
  std::vector<uint8_t> vector;
  assert(!read_ready_resource(d.id, vector) && vector.empty());

  // The current namespace changes after a scan but before deletion.
  reset(); files[old_path] = "old";
  set_private_resource_scope(1, "B");
  list_hook = [] { set_private_resource_scope(1, "A"); };
  drain_jobs();
  assert(files.count(old_path) == 1 && runtime.cleanup_pending);
  clock_ms += 31000; drain_jobs();
  assert(files.count(old_path) == 1 && !runtime.cleanup_pending);

  // OTA cancellation must release the cleanup handle, while preserving the
  // pending work so later resource activity can admit a fresh cleanup job.
  reset(); files[old_path] = "old"; busy_path = old_path;
  set_private_resource_scope(1, "B");
  delete_hook = [] { std::string error; assert(begin_network_ota_maintenance(error)); };
  drain_jobs();
  assert(runtime.cleanup_pending && !runtime.cleanup_job);
  end_network_ota_maintenance(); busy_path.clear();
  assert(request_resource(d)); drain_jobs();
  assert(files.count(old_path) == 0 && !runtime.cleanup_pending);
  // Cancellation after the resource runner's last check, before scheduler
  // completion, can retire the scheduler record without clearing our handle.
  reset(); files[old_path] = "old"; busy_path = old_path;
  set_private_resource_scope(1, "B");
  std::string cancellation_error;
  assert(network_job_active(runtime.cleanup_job));
  assert(cancel_network_job(runtime.cleanup_job, cancellation_error));
  assert(!network_job_active(runtime.cleanup_job));
  NetworkJobDefinition tail_cancel_job;
  tail_cancel_job.owner_service_id = "resource.cleanup";
  tail_cancel_job.kind = "test_tail_cancel";
  tail_cancel_job.maximum_attempts = 255;
  tail_cancel_job.runner = [](NetworkJobHandle handle, void *) {
    assert(network_job_active(handle));
    const auto result = run_private_cleanup(handle, nullptr);
    assert(result.retryable && runtime.cleanup_job);
    std::string error;
    assert(begin_network_ota_maintenance(error));
    assert(network_job_active(handle)); // Still owned until this runner returns.
    return result;
  };
  assert(submit_network_job(tail_cancel_job, runtime.cleanup_job, cancellation_error));
  drain_jobs();
  assert(runtime.cleanup_pending && runtime.cleanup_job);
  assert(!network_job_active(runtime.cleanup_job));
  end_network_ota_maintenance(); busy_path.clear();
  assert(request_resource(d)); drain_jobs();
  assert(files.count(old_path) == 0 && !runtime.cleanup_pending && !runtime.cleanup_job);
  std::cout << "Resource service cache-hit races, scope cleanup/retry, read fences and corrupt-size recovery passed\n";
}
