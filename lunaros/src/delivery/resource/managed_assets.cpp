#include "lunaredu_lunaros/delivery/managed_assets.h"
#include "lunaredu_lunaros/persistence/storage_pool.h"
#include "lunaredu_lunaros/persistence/storage.h"
#include "lunaredu_lunaros/network/network_jobs.h"
#include "lunaredu_lunaros/network/http_client.h"
#include "delivery/resource/managed_asset_formats.h"
#include "delivery/resource/managed_asset_record.h"
#include <array>
#include <mutex>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string_view>
#include <esp_heap_caps.h>
#include <psa/crypto.h>

namespace lunaredu::lunaros {
namespace {
using internal::ManagedAssetRecord;
using internal::read_managed_asset_record;
using internal::save_managed_asset_record;
constexpr std::array<const char *, kManagedAssetCount> ids = {
    "fonts", "human_face_det", "human_face_feat", "hand_det", "hand_gesture_cls", "model"};
struct Slot { NetworkJobHandle job; std::string error; };
std::array<Slot, ids.size()> slots;
std::array<std::atomic<uint32_t>, ids.size()> generations{};
std::recursive_mutex mutex;
// Each resource owns its publication/read lock. An unrelated download must
// not block a ready font or model; same-resource reads stay serialized.
std::array<std::mutex, ids.size()> transactions;
int index_of(std::string_view id) {
  for (size_t i = 0; i < ids.size(); ++i) if (id == ids[i]) return static_cast<int>(i);
  return -1;
}
std::string base(const char *id) { return std::string("/managed/") + id; }
bool valid(const ManagedAssetRequest &r, bool require_url = true) {
  return index_of(r.id) >= 0 && !r.revision.empty() && r.revision.size() <= 64 &&
      ((!require_url && r.url.empty()) || r.url.rfind("https://", 0) == 0) && r.url.size() <= 1024 &&
      r.bytes >= 16 && r.bytes <= kStoragePoolBytes - kStoragePoolHeadroom &&
      r.sha256.size() == 64 && std::all_of(r.sha256.begin(), r.sha256.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
      });
}
struct ReadContext {
  uint8_t *data; size_t capacity; size_t used = 0;
  psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
};
bool read_chunk(const uint8_t *data, size_t size, void *opaque) {
  auto &r = *static_cast<ReadContext *>(opaque);
  if (size > r.capacity - r.used || psa_hash_update(&r.hash, data, size) != PSA_SUCCESS) return false;
  if (r.data) std::copy_n(data, size, r.data + r.used);
  r.used += size;
  return true;
}
bool finish_hash(ReadContext &r, const std::string &expected) {
  uint8_t digest[32]; size_t size = 0;
  const auto status = psa_hash_finish(&r.hash, digest, sizeof(digest), &size);
  if (status != PSA_SUCCESS || size != 32) return false;
  const char *hex = "0123456789abcdef";
  std::string text;
  for (auto byte : digest) { text += hex[byte >> 4]; text += hex[byte & 15]; }
  return text == expected;
}
void *read_verified(const ManagedAssetRequest &request, const std::string &path, std::string &error) {
  if (psa_crypto_init() != PSA_SUCCESS) { error = "asset_crypto_init_failed"; return nullptr; }
  auto *data = static_cast<uint8_t *>(heap_caps_aligned_alloc(16, request.bytes,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!data) { error = "asset_psram_unavailable"; return nullptr; }
  ReadContext context{data, request.bytes};
  bool ok = psa_hash_setup(&context.hash, PSA_ALG_SHA_256) == PSA_SUCCESS &&
      storage_stream_file(path.c_str(), read_chunk, &context, &error) &&
      context.used == request.bytes && finish_hash(context, request.sha256) &&
      internal::managed_asset_format_valid(request.id, data, request.bytes);
  psa_hash_abort(&context.hash);
  if (!ok) { heap_caps_free(data); error = "asset_verification_failed"; return nullptr; }
  return data;
}
bool verify_download_file(const ManagedAssetRequest &request, const std::string &path, std::string &error) {
  if (psa_crypto_init() != PSA_SUCCESS) { error = "asset_crypto_init_failed"; return false; }
  ReadContext context{nullptr, request.bytes};
  bool valid = psa_hash_setup(&context.hash, PSA_ALG_SHA_256) == PSA_SUCCESS &&
      storage_stream_file(path.c_str(), read_chunk, &context, &error) &&
      context.used == request.bytes && finish_hash(context, request.sha256);
  psa_hash_abort(&context.hash);
  if (!valid) { error = "asset_verification_failed"; return false; }
  auto read = [](size_t offset, uint8_t *data, size_t size, void *opaque) {
    return storage_read_range(static_cast<const char *>(opaque), offset, data, size);
  };
  valid = internal::managed_asset_file_valid(request.id, request.bytes, read, const_cast<char *>(path.c_str()));
  if (!valid) error = "asset_format_invalid";
  return valid;
}
struct Download {
  NetworkDownloadReader *reader; NetworkJobHandle job; size_t remaining;
};
bool produce(StorageFileChunkWriter writer, void *target, void *opaque, std::string &error) {
  auto &d = *static_cast<Download *>(opaque);
  std::array<char, 1024> bytes;
  for (;;) {
    if (network_job_cancellation_requested(d.job)) { error = "asset_download_cancelled"; return false; }
    const int count = d.reader->Read(bytes.data(), bytes.size());
    if (count < 0) { error = "asset_network_read_failed"; return false; }
    if (!count) return d.remaining == 0;
    if (static_cast<size_t>(count) > d.remaining) { error = "asset_length_mismatch"; return false; }
    if (!writer(reinterpret_cast<uint8_t *>(bytes.data()), count, target)) return false;
    d.remaining -= count;
  }
}
NetworkJobRunResult run(NetworkJobHandle handle, void *opaque) {
  const size_t index = static_cast<Slot *>(opaque) - slots.data();
  std::lock_guard<std::mutex> work(transactions[index]);
  ManagedAssetRecord record;
  std::string error;
  if (!read_managed_asset_record(ids[index], record)) return {false, false, "asset_record_missing"};
  const auto r = record.request;
  auto fail = [&](const std::string &reason, bool retry = true) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    slots[index].error = reason;
    return NetworkJobRunResult{false, retry, reason};
  };
  if (!valid(r)) return fail("asset_record_invalid", false);
  if (record.ready) return {true, false, ""};
  const auto path = managed_asset_path(ids[index]);
  const auto part = path + ".part";
  // Publication was invalidated durably before this job was submitted.
  if (storage_path_exists(path.c_str()) && !storage_delete_file(path.c_str())) return fail("asset_file_busy");
  if (storage_path_exists(part.c_str()) && !storage_delete_file(part.c_str())) return fail("asset_partial_busy");
  if (!storage_pool_admit(r.bytes, true, error)) return fail(error);
  NetworkDownloadRequest request;
  request.url = r.url;
  request.timeout_ms = 5000;
  request.transport_security = NetworkTransportSecurity::RequireTls;
  auto reader = network_start_download(std::move(request));
  if (!reader) return fail("asset_download_open_failed");
  if (reader->content_length() != r.bytes) { reader->Close(); return fail("asset_length_mismatch", false); }
  Download download{reader.get(), handle, r.bytes};
  const bool written = storage_write_streamed_file(part.c_str(), produce, &download, nullptr, &error);
  reader->Close();
  if (!written) return fail(error.empty() ? "asset_download_failed" : error);
  if (!verify_download_file(r, part, error)) { storage_delete_file(part.c_str()); return fail(error); }
  if (network_job_cancellation_requested(handle)) return fail("asset_download_cancelled");
  if (!storage_move_file(part.c_str(), path.c_str(), &error)) return fail(error);
  record.ready = true;
  if (!save_managed_asset_record(ids[index], record)) return fail("asset_publication_failed");
  std::lock_guard<std::recursive_mutex> guard(mutex);
  slots[index].error.clear();
  return {true, false, "asset_installed_restart_to_activate"};
}
bool queue(size_t index, std::string &error) {
  NetworkJobDefinition definition;
  definition.owner_service_id = ids[index];
  definition.kind = "managed_asset";
  definition.priority = kNetworkPriorityBackground;
  definition.maximum_attempts = 255;
  definition.retry_backoff_initial_ms = 5000;
  definition.retry_backoff_maximum_ms = 60000;
  definition.runner = run;
  definition.context = &slots[index];
  return submit_network_job(definition, slots[index].job, error);
}
} // namespace
std::string managed_asset_path(const char *id) {
  return id && index_of(id) >= 0 ? base(id) + ".bin" : std::string{};
}
bool request_managed_asset(const ManagedAssetRequest &r, std::string &error) {
  if (!storage_pool_enabled() || !valid(r)) { error = "asset_request_invalid"; return false; }
  const auto index = index_of(r.id);
  std::unique_lock<std::mutex> work(transactions[index], std::try_to_lock);
  if (!work.owns_lock()) { error = "asset_operation_busy"; return false; }
  std::lock_guard<std::recursive_mutex> guard(mutex);
  if (slots[index].job && network_job_active(slots[index].job)) { error = "asset_download_active"; return false; }
  ManagedAssetRecord record{r, false};
  if (!save_managed_asset_record(r.id.c_str(), record)) { error = "asset_record_save_failed"; return false; }
  ++generations[index];
  return queue(index, error);
}
bool remove_managed_asset(const std::string &id, std::string &error) {
  const auto index = index_of(id);
  if (index < 0) { error = "asset_id_invalid"; return false; }
  std::lock_guard<std::recursive_mutex> guard(mutex);
  if (slots[index].job && network_job_active(slots[index].job)) {
    (void)cancel_network_job(slots[index].job, error);
    error = "asset_cancelling_retry_remove"; return false;
  }
  std::unique_lock<std::mutex> work(transactions[index], std::try_to_lock);
  if (!work.owns_lock()) { error = "asset_operation_busy"; return false; }
  // Remove publication first. Power loss cannot make partially deleted data ready.
  ++generations[index];
  for (const char *suffix : {".json", ".bin", ".bin.part", ".tmp"}) {
    const auto path = base(id.c_str()) + suffix;
    if (storage_path_exists(path.c_str()) && !storage_delete_file(path.c_str())) { error = "asset_remove_failed"; return false; }
  }
  slots[index] = {};
  return true;
}
void resume_managed_assets() {
  if (!storage_pool_enabled()) return;
  std::lock_guard<std::recursive_mutex> guard(mutex);
  for (size_t i = 0; i < ids.size(); ++i) {
    ManagedAssetRecord record;
    if (read_managed_asset_record(ids[i], record) && !record.ready &&
        valid(record.request) && !network_job_active(slots[i].job)) {
      (void)queue(i, slots[i].error);
    }
  }
}
ManagedAssetCatalogSnapshot managed_assets_snapshot() {
  std::lock_guard<std::recursive_mutex> guard(mutex);
  ManagedAssetCatalogSnapshot snapshot;
  snapshot.pool_enabled = storage_pool_enabled();
  snapshot.user_reserve_bytes = storage_pool_user_reserve();
  snapshot.headroom_bytes = kStoragePoolHeadroom;
  snapshot.total_bytes = storage_total_bytes();
  snapshot.used_bytes = storage_used_bytes();
  snapshot.user_used_bytes = storage_pool_user_used();
  for (size_t i = 0; i < ids.size(); ++i) {
    auto &item = snapshot.assets[i];
    item.id = ids[i];
    ManagedAssetRecord record;
    const bool present = read_managed_asset_record(ids[i], record);
    item.state = !present ? ManagedAssetState::Absent :
        record.ready ? ManagedAssetState::Ready : ManagedAssetState::Invalid;
    item.downloading = network_job_active(slots[i].job);
    item.error = slots[i].error;
    item.bytes = record.request.bytes;
    item.revision = record.request.revision;
  }
  return snapshot;
}
bool managed_asset_ready(const char *id) {
  if (!id || index_of(id) < 0) return false;
  ManagedAssetRecord record;
  return read_managed_asset_record(id, record) && record.ready && valid(record.request, false);
}
uint32_t managed_asset_generation(const char *id) {
  const int index = id ? index_of(id) : -1;
  return index < 0 ? 0 : generations[index].load();
}
void *load_managed_asset(const char *id, size_t &bytes, std::string &error) {
  bytes = 0;
  const int index = id ? index_of(id) : -1;
  if (index < 0) { error = "asset_id_invalid"; return nullptr; }
  std::lock_guard<std::mutex> work(transactions[index]);
  ManagedAssetRecord record;
  if (!read_managed_asset_record(id, record) || !record.ready) { error = "asset_not_ready"; return nullptr; }
  const auto request = record.request;
  if (!valid(request, false)) { error = "asset_record_invalid"; return nullptr; }
  auto *data = read_verified(request, managed_asset_path(id), error);
  if (data) bytes = request.bytes;
  // Allocation failure is transient and must not invalidate a good publication.
  else if (error == "asset_verification_failed") { record.ready = false; (void)save_managed_asset_record(id, record); ++generations[index_of(id)]; }
  return data;
}
} // namespace lunaredu::lunaros
