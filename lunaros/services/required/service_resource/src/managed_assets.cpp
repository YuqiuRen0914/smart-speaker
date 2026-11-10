#include "resource_runtime_internal.h"
#include "download_stream.h"
#include "lunaros_resource_managed_assets.h"
#include "lunaros_storage_pool.h"
#include "lunaros_storage.h"
#include "lunaros_network_jobs.h"
#include "lunaros_network_http_client.h"
#include "managed_asset_formats.h"
#include "managed_asset_compression.h"
#include "managed_asset_record.h"
#include <array>
#include <cstring>
#include <mutex>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string_view>
#include <esp_heap_caps.h>
#include "lunaros_digest.h"

namespace lunaredu::lunaros {
namespace hashing = ::lunaredu::lunaros::digest;
namespace {
using internal::ManagedAssetRecord;
using internal::read_managed_asset_record;
using internal::save_managed_asset_record;
std::array<const char *, kManagedAssetCapacity> ids{};
std::array<ManagedAssetFormat, kManagedAssetCapacity> formats{};
std::array<const char *const *, kManagedAssetCapacity> model_families{};
std::array<size_t, kManagedAssetCapacity> model_family_counts{};
size_t asset_count = 0;
bool catalog_frozen = false;
struct Slot {
  NetworkJobHandle job;
  std::string error;
  size_t downloaded_bytes = 0;
  bool activation_pending = false;
  ManagedAssetRequest rejected_request{};
  std::string source_rejection{};
};
std::array<Slot, ids.size()> slots;
std::array<std::atomic<uint32_t>, ids.size()> generations{};
std::recursive_mutex mutex;
// Each resource owns its publication/read lock. An unrelated download must
// not block a ready font or model; same-resource reads stay serialized.
std::array<std::mutex, ids.size()> transactions;
using UpdateBusyProbe = bool (*)();
std::atomic<UpdateBusyProbe> update_busy_probe{nullptr};
bool admit_mutation(std::string &error) {
  auto probe=update_busy_probe.load();
  if(!probe){error="asset_update_policy_missing";return false;}
  if(probe()){error="asset_firmware_update_busy";return false;}
  return true;
}
int index_of(std::string_view id) {
  for (size_t i = 0; i < asset_count; ++i) if (id == ids[i]) return static_cast<int>(i);
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
  hashing::Sha256 hash{};
  bool hash_failed = false;
  bool length_failed = false;
};
bool read_chunk(const uint8_t *data, size_t size, void *opaque) {
  auto &r = *static_cast<ReadContext *>(opaque);
  if (size > r.capacity - r.used) { r.length_failed = true; return false; }
  if (!r.hash.update(data,size)) { r.hash_failed = true; return false; }
  if (r.data) std::copy_n(data, size, r.data + r.used);
  r.used += size;
  return true;
}
bool finish_hash(ReadContext &r, const std::string &expected) {
  hashing::Sha256Value value{};
  if (!r.hash.finish(value)) { r.hash_failed = true; return false; }
  return hashing::hex(value.data(),value.size()) == expected;
}
std::string verification_error(const ReadContext &context, bool streamed) {
  if (context.hash_failed)
    return "asset_crypto_failed_psa_" + std::to_string(context.hash.last_status());
  if (!streamed && !context.length_failed) return "asset_read_failed";
  return "asset_verification_failed";
}
void *read_verified(const ManagedAssetRequest &request, const std::string &path, size_t &bytes, std::string &error) {
  bytes = 0;
  if (!hashing::initialize()) { error = "asset_crypto_init_failed"; return nullptr; }
  auto *data = static_cast<uint8_t *>(heap_caps_aligned_alloc(16, request.bytes,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!data) { error = "asset_psram_unavailable"; return nullptr; }
  ReadContext context{data, request.bytes};
  context.hash_failed = !context.hash.begin();
  const bool streamed = !context.hash_failed &&
      storage_stream_file(path.c_str(), read_chunk, &context, &error);
  bool ok = streamed &&
      context.used == request.bytes && finish_hash(context, request.sha256);
  (void)context.hash.abort();
  if (!ok) { heap_caps_free(data); error = verification_error(context, streamed); return nullptr; }
  size_t decoded = request.bytes;
  if (!internal::decode_managed_asset(data, decoded, error)) { heap_caps_free(data); return nullptr; }
  const size_t index = index_of(request.id);
  if (!internal::managed_asset_format_valid(formats[index], data, decoded,
          model_families[index], model_family_counts[index])) {
    heap_caps_free(data); error = "asset_verification_failed"; return nullptr;
  }
  bytes = decoded;
  return data;
}
bool verify_download_file(const ManagedAssetRequest &request, const std::string &path, std::string &error) {
  uint8_t header[4] = {};
  if (storage_read_range(path.c_str(), 0, header, sizeof(header)) &&
      internal::compressed_asset(header, sizeof(header))) {
    size_t decoded = 0;
    auto *data = read_verified(request, path, decoded, error);
    if (!data) return false;
    heap_caps_free(data); return true;
  }
  if (!hashing::initialize()) { error = "asset_crypto_init_failed"; return false; }
  ReadContext context{nullptr, request.bytes};
  context.hash_failed = !context.hash.begin();
  const bool streamed = !context.hash_failed &&
      storage_stream_file(path.c_str(), read_chunk, &context, &error);
  bool valid = streamed &&
      context.used == request.bytes && finish_hash(context, request.sha256);
  (void)context.hash.abort();
  if (!valid) { error = verification_error(context, streamed); return false; }
  auto read = [](size_t offset, uint8_t *data, size_t size, void *opaque) {
    return storage_read_range(static_cast<const char *>(opaque), offset, data, size);
  };
  const size_t index = index_of(request.id);
  valid = internal::managed_asset_file_valid(formats[index], request.bytes, read,
      const_cast<char *>(path.c_str()), model_families[index], model_family_counts[index]);
  if (!valid) error = "asset_format_invalid";
  return valid;
}
// The intent is committed before replacing bytes. If power fails between the
// data rename and publication metadata, startup can finish that same publish
// after validating its exact bytes. No old publication is deleted for space.
bool publish_verified(ManagedAssetRecord record,const std::string &part,std::string &error) {
  if(!admit_mutation(error))return false;
  record.ready=true;
  if(!internal::save_pending_managed_asset_record(record.request.id.c_str(),record)) {
    error="asset_publication_intent_failed";return false;
  }
  const auto path=base(record.request.id.c_str())+".bin";
  if(!admit_mutation(error)||!storage_move_file(part.c_str(),path.c_str(),&error))return false;
  if(!save_managed_asset_record(record.request.id.c_str(),record)) {
    error="asset_publication_failed";return false;
  }
  (void)storage_delete_file((base(record.request.id.c_str())+".pending.json").c_str());
  {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    const auto index = index_of(record.request.id);
    slots[index].source_rejection.clear(); slots[index].rejected_request = {};
  }
  return true;
}
bool valid_import_source(const std::string &source) {
  return source.size() > 6 && source.size() <= 192 && source.back() != '/' && source.rfind("/user/", 0) == 0 &&
      source.find("..") == std::string::npos && source.find('\0') == std::string::npos && source.find('\\') == std::string::npos &&
      storage_is_safe_path(source.c_str());
}
bool same_request(const ManagedAssetRequest &a, const ManagedAssetRequest &b) {
  return a.id == b.id && a.revision == b.revision && a.url == b.url &&
      a.sha256 == b.sha256 && a.bytes == b.bytes;
}
bool same_content(const ManagedAssetRequest &a, const ManagedAssetRequest &b) {
  return a.id == b.id && a.revision == b.revision && a.sha256 == b.sha256 && a.bytes == b.bytes;
}
std::string source_rejection_error(const ManagedAssetRecord &record) {
  return "asset_source_refresh_required:" + record.source_error +
      (record.source_http_status ? ":" + std::to_string(record.source_http_status) : "");
}
bool permanent_content_failure(const std::string &error) {
  return error == "asset_length_mismatch" || error == "asset_verification_failed" ||
      error == "asset_format_invalid" || error == "asset_compression_unavailable";
}
// Called with the resource transaction lock, without holding the catalog lock
// in request paths. Storage move atomically captures the source only when no
// stream lease owns it; all validation happens on that private captured file.
bool finish_import(internal::ManagedAssetImportIntent intent, std::string &error) {
  auto &record = intent.record;
  const auto &r = record.request;
  if (!valid(r, false) || !r.url.empty() || !valid_import_source(intent.source)) {
    error = "asset_import_intent_invalid"; return false;
  }
  const auto prefix = base(r.id.c_str());
  const auto part = prefix + ".import.part";
  const auto journal = prefix + ".import.json";
  if (!admit_mutation(error)) return false;
  if (!storage_path_exists(part.c_str())) {
    if (record.ready) {
      // The validated source was already renamed to the final target. Finish
      // its metadata commit, never consume a newly uploaded public file here.
      if (!verify_download_file(r, prefix + ".bin", error) || !admit_mutation(error)) return false;
      if (!save_managed_asset_record(r.id.c_str(), record)) {
        error = "asset_publication_failed"; return false;
      }
      (void)storage_delete_file((prefix + ".pending.json").c_str());
      if (!storage_delete_file(journal.c_str())) { error = "asset_import_cleanup_failed"; return false; }
      return true;
    }
    if (!storage_move_file(intent.source.c_str(), part.c_str(), &error)) return false;
  }
  // These bytes already occupy the pool. Recalculate actual usage, reserve and
  // metadata headroom after the move, without reserving a second payload copy.
  if (!storage_pool_admit(0, true, error)) return false;
  if (!verify_download_file(r, part, error)) {
    // A rejected source is consumed, but never replaces the old publication.
    // Retain captured bytes on transient read/crypto/PSRAM failures for retry.
    // Withdraw the intent before deleting rejected staging; a cleanup failure
    // must not authorize consuming a newly uploaded file after restart.
    const bool rejected = permanent_content_failure(error);
    if (rejected && admit_mutation(error) && storage_delete_file(journal.c_str()))
      (void)storage_delete_file(part.c_str());
    return false;
  }
  if (!admit_mutation(error)) return false;
  record.ready = true;
  if (!internal::save_managed_asset_import_intent(r.id.c_str(), intent)) {
    error = "asset_import_intent_failed"; return false;
  }
  if (!publish_verified(record, part, error)) return false;
  if (!storage_delete_file(journal.c_str())) { error = "asset_import_cleanup_failed"; return false; }
  return true;
}
NetworkJobRunResult run(NetworkJobHandle handle, void *opaque) {
  const size_t index = static_cast<Slot *>(opaque) - slots.data();
  std::lock_guard<std::mutex> work(transactions[index]);
  ManagedAssetRecord record;
  std::string error;
  const bool record_read = internal::read_pending_managed_asset_record(ids[index], record);
  const auto r = record.request;
  auto fail = [&](const std::string &reason, bool retry = true) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    slots[index].error = reason;
    slots[index].downloaded_bytes = 0;
    return NetworkJobRunResult{false, retry, reason};
  };
  auto succeeded = [&] {
    ++generations[index];
    std::lock_guard<std::recursive_mutex> guard(mutex);
    slots[index].error.clear(); slots[index].source_rejection.clear();
    slots[index].rejected_request = {}; slots[index].downloaded_bytes = r.bytes;
    slots[index].activation_pending = true;
    return NetworkJobRunResult{true, false, "asset_installed_restart_to_activate"};
  };
  if (!record_read || !valid(r)) return fail("asset_record_invalid", false);
  if (record.source_rejected) return fail(source_rejection_error(record), false);
  auto reject_source = [&](const std::string &reason, int status = 0) {
    record.ready = false;
    record.source_rejected = true;
    record.source_error = reason;
    record.source_http_status = status;
    std::string rejected = source_rejection_error(record);
    if (!admit_mutation(error) || !internal::save_pending_managed_asset_record(ids[index], record))
      rejected = "asset_source_rejection_persist_failed:" + rejected;
    // Persist failure is terminal too. The volatile guard prevents this boot's
    // observer or recovery upkeep from resetting the same rejected source.
    {
      std::lock_guard<std::recursive_mutex> guard(mutex);
      slots[index].rejected_request = r;
      slots[index].source_rejection = rejected;
    }
    return fail(rejected, false);
  };
  if(!admit_mutation(error))return fail(error,false);
  if (record.ready) {
    // Retry the verified publication before opening its possibly expired URL.
    // A data rename may already have committed while metadata still needs I/O.
    const auto path = managed_asset_path(ids[index]);
    if (verify_download_file(r, path, error)) {
      if (!admit_mutation(error)) return fail(error, false);
      if (!save_managed_asset_record(ids[index], record)) return fail("asset_publication_failed");
      (void)storage_delete_file((base(ids[index])+".pending.json").c_str());
      return succeeded();
    }
    if (verify_download_file(r, path+".part", error)) {
      if (!publish_verified(record, path+".part", error)) return fail(error);
      return succeeded();
    }
    return fail("asset_publication_recovery_failed:" + error);
  }
  {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    slots[index].downloaded_bytes = 0;
    slots[index].error.clear();
  }
  const auto path = managed_asset_path(ids[index]);
  const auto part = path + ".part";
  // Keep current bytes and metadata until the complete replacement validates.
  if (storage_path_exists(part.c_str()) && !storage_delete_file(part.c_str())) return fail("asset_partial_busy");
  if (!storage_pool_admit(r.bytes, true, error)) return fail(error);
  DownloadRequest request;
  request.url = r.url;
  request.maximum_bytes = r.bytes;
  request.timeout_ms = 5000;
  request.transport_security = DownloadTransportSecurity::RequireTls;
  DownloadFailure download_failure;
  auto reader = download_start(std::move(request), &download_failure);
  if (!reader) {
    const int status = download_failure.status_code;
    if (status == 401 || status == 403 || status == 404 || status == 410)
      return reject_source("asset_download_http_rejected", status);
    if (download_failure.detail == "download_size_limit_exceeded" ||
        download_failure.detail == "download_content_length_unavailable")
      return reject_source("asset_length_mismatch");
    return fail(status > 0 ? "asset_download_open_failed:http_" + std::to_string(status)
                           : "asset_download_open_failed");
  }
  if (reader->content_length() != r.bytes) { reader->Close(); return reject_source("asset_length_mismatch"); }
  struct ProgressContext { size_t index; NetworkJobHandle handle; };
  const ProgressContext progress_context{index, handle};
  internal::DownloadStream download{reader.get(), r.bytes, 0, nullptr,
      [](const void *opaque) { return network_job_cancellation_requested(*static_cast<const NetworkJobHandle *>(opaque)); },
      &handle, true,
      [](size_t bytes, const void *opaque) {
        const auto &progress = *static_cast<const ProgressContext *>(opaque);
        std::lock_guard<std::recursive_mutex> guard(mutex);
        if (slots[progress.index].job.id == progress.handle.id)
          slots[progress.index].downloaded_bytes = bytes;
      }, &progress_context};
  const bool written = storage_write_streamed_file(part.c_str(), internal::copy_download_stream, &download, nullptr, &error);
  reader->Close();
  if (!written) {
    // Premature EOF can mean a dropped connection; preserve bounded retries.
    // An oversized response or advertised length mismatch is definitive.
    if (error == "resource_too_large") return reject_source("asset_length_mismatch");
    return fail(error.empty() ? "asset_download_failed" : error);
  }
  if (!verify_download_file(r, part, error)) {
    storage_delete_file(part.c_str());
    return permanent_content_failure(error) ? reject_source(error) : fail(error);
  }
  if (network_job_cancellation_requested(handle)) return fail("asset_download_cancelled");
  if (!publish_verified(record,part,error)) return fail(error);
  return succeeded();
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
void set_managed_asset_update_busy_probe(bool (*probe)()) {update_busy_probe.store(probe);}
bool managed_asset_updates_admitted() { std::string error; return admit_mutation(error); }
bool managed_asset_operations_idle() {
  std::array<std::unique_lock<std::mutex>, kManagedAssetCapacity> locks;
  for(size_t i=0;i<asset_count;++i) {
    locks[i]=std::unique_lock<std::mutex>(transactions[i],std::try_to_lock);
    if(!locks[i].owns_lock())return false;
  }
  return true;
}
bool configure_managed_assets(const ManagedAssetDefinition *assets, size_t count,
                              std::string &error) {
  std::lock_guard<std::recursive_mutex> guard(mutex);
  error.clear();
  if (catalog_frozen) {
    // A failed startup may enter management with the same immutable catalog.
    // Re-declaring identical policy does not mutate assets or reset work.
    bool same = count == asset_count && (count == 0 || assets);
    for (size_t i = 0; same && i < count; ++i) {
      same = assets[i].id && std::strcmp(ids[i], assets[i].id) == 0 &&
          formats[i] == assets[i].format &&
          model_family_counts[i] == assets[i].model_family_count &&
          (assets[i].model_family_count == 0 || assets[i].model_families);
      for (size_t f = 0; same && f < assets[i].model_family_count; ++f)
        same = assets[i].model_families[f] &&
            std::strcmp(model_families[i][f], assets[i].model_families[f]) == 0;
    }
    if (same) return true;
    error = "asset_catalog_frozen";
    return false;
  }
  if (count > ids.size() || (count && !assets)) { error = "invalid_asset_catalog"; return false; }
  for (size_t i = 0; i < count; ++i) {
    if (!assets[i].id || !*assets[i].id || std::strlen(assets[i].id) > 48 ||
        static_cast<unsigned>(assets[i].format) > static_cast<unsigned>(ManagedAssetFormat::SpeechPack)) {
      error = "invalid_asset_definition"; return false;
    }
    for (const char *p = assets[i].id; *p; ++p) {
      if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) {
        error = "invalid_asset_id"; return false;
      }
    }
    for (size_t j = 0; j < i; ++j) if (std::strcmp(assets[i].id, assets[j].id) == 0) {
      error = "duplicate_asset_id"; return false;
    }
    if ((assets[i].model_families == nullptr) != (assets[i].model_family_count == 0) ||
        assets[i].model_family_count > 8 ||
        (assets[i].model_family_count && assets[i].format != ManagedAssetFormat::SpeechPack)) {
      error = "invalid_asset_model_policy"; return false;
    }
    for (size_t f = 0; f < assets[i].model_family_count; ++f) {
      const char *family = assets[i].model_families[f];
      if (!family || !*family || std::strlen(family) > 16) {
        error = "invalid_asset_model_policy"; return false;
      }
      for (const char *p = family; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9'))) {
          error = "invalid_asset_model_policy"; return false;
        }
      }
    }
  }
  for (size_t i = 0; i < count; ++i) {
    ids[i] = assets[i].id; formats[i] = assets[i].format;
    model_families[i] = assets[i].model_families; model_family_counts[i] = assets[i].model_family_count;
  }
  asset_count = count;
  catalog_frozen = true;
  return true;
}
std::string managed_asset_path(const char *id) {
  return id && index_of(id) >= 0 ? base(id) + ".bin" : std::string{};
}
bool request_managed_asset(const ManagedAssetRequest &r, std::string &error) {
  error.clear();
  if(!admit_mutation(error))return false;
  if (!storage_pool_enabled() || !valid(r)) { error = "asset_request_invalid"; return false; }
  const auto index = index_of(r.id);
  std::unique_lock<std::mutex> work(transactions[index], std::try_to_lock);
  if (!work.owns_lock()) { error = "asset_operation_busy"; return false; }
  std::lock_guard<std::recursive_mutex> guard(mutex);
  if (storage_path_exists((base(r.id.c_str()) + ".import.json").c_str())) {
    error = "asset_import_pending"; return false;
  }
  ManagedAssetRecord previous;
  const auto pending_path = base(r.id.c_str()) + ".pending.json";
  const bool pending = internal::read_pending_managed_asset_record(r.id.c_str(), previous);
  if (!pending && storage_path_exists(pending_path.c_str())) {
    error = "asset_pending_record_invalid"; return false;
  }
  if (!slots[index].source_rejection.empty() && same_request(slots[index].rejected_request, r)) {
    error = slots[index].source_rejection; return false;
  }
  if (pending && previous.source_rejected && same_request(previous.request, r)) {
    error = source_rejection_error(previous); slots[index].error = error; return false;
  }
  const bool active = slots[index].job && network_job_active(slots[index].job);
  if (pending && previous.ready) {
    // Never overwrite the only durable proof for an already renamed target.
    if (!active && !queue(index, error)) return false;
    error = "asset_publication_pending"; return false;
  }
  if (active) {
    // The transaction lock proves the worker is not reading the descriptor.
    // Keep its existing queued/backoff attempt budget when only URL rotates.
    if (!pending || previous.ready || previous.source_rejected || !same_content(previous.request, r)) {
      error = "asset_download_active"; return false;
    }
    if (same_request(previous.request, r)) { error = "asset_download_active"; return false; }
  }
  ManagedAssetRecord record{r, false};
  if (!internal::save_pending_managed_asset_record(r.id.c_str(), record)) {
    error = "asset_record_save_failed"; return false;
  }
  slots[index].source_rejection.clear(); slots[index].rejected_request = {};
  slots[index].error.clear(); slots[index].downloaded_bytes = 0;
  slots[index].activation_pending = false;
  return active || queue(index, error);
}
bool cancel_managed_asset_update(const std::string &id, std::string &error) {
  error.clear();
  if (!admit_mutation(error)) return false;
  const int index = index_of(id);
  if (index < 0) { error = "asset_id_invalid"; return false; }
  std::lock_guard<std::recursive_mutex> guard(mutex);
  if (slots[index].job && network_job_active(slots[index].job)) {
    if (!cancel_network_job(slots[index].job, error)) return false;
    error = "asset_cancellation_pending"; return false;
  }
  std::unique_lock<std::mutex> work(transactions[index], std::try_to_lock);
  if (!work.owns_lock()) { error = "asset_operation_busy"; return false; }
  ManagedAssetRecord record;
  const auto pending = base(id.c_str()) + ".pending.json";
  if (!internal::read_pending_managed_asset_record(id.c_str(), record)) {
    if (storage_path_exists(pending.c_str())) { error = "asset_pending_record_invalid"; return false; }
    return true;
  }
  // A verified publication may already have renamed its target. Finish that
  // transaction through recovery instead of discarding its durable proof.
  if (record.ready || storage_path_exists((base(id.c_str()) + ".import.json").c_str())) {
    error = "asset_publication_pending"; return false;
  }
  record.source_rejected = true;
  record.source_error = "asset_download_cancelled";
  record.source_http_status = 0;
  if (!internal::save_pending_managed_asset_record(id.c_str(), record)) {
    error = "asset_cancellation_commit_failed"; return false;
  }
  slots[index].rejected_request = record.request;
  slots[index].source_rejection = source_rejection_error(record);
  slots[index].error = "asset_download_cancelled";
  return true;
}
bool remove_managed_asset(const std::string &id, std::string &error) {
  if(!admit_mutation(error))return false;
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
  for (const char *suffix : {".json", ".bin", ".bin.part", ".tmp", ".json.tmp", ".pending.json", ".pending.json.tmp", ".import.json", ".import.json.tmp", ".import.part"}) {
    if(!admit_mutation(error))return false;
    const auto path = base(id.c_str()) + suffix;
    if (storage_path_exists(path.c_str()) && !storage_delete_file(path.c_str())) { error = "asset_remove_failed"; return false; }
  }
  slots[index] = {};
  return true;
}
bool import_managed_asset(const ManagedAssetRequest &r, const std::string &source,
                          std::string &error, bool consume_source) {
  error.clear();
  if(!admit_mutation(error))return false;
  if (!storage_pool_enabled() || !valid(r, false) || !r.url.empty() ||
      !valid_import_source(source)) {
    error = "asset_import_invalid"; return false;
  }
  const size_t index = static_cast<size_t>(index_of(r.id));
  std::unique_lock<std::mutex> work(transactions[index], std::try_to_lock);
  if (!work.owns_lock()) { error = "asset_operation_busy"; return false; }
  {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if (slots[index].job && network_job_active(slots[index].job)) {
      error = "asset_download_active"; return false;
    }
  }
  const auto journal = base(r.id.c_str()) + ".import.json";
  if (consume_source) {
    internal::ManagedAssetImportIntent intent;
    if (storage_path_exists(journal.c_str())) {
      if (!internal::read_managed_asset_import_intent(r.id.c_str(), intent) ||
          intent.source != source || !same_request(intent.record.request, r)) {
        error = "asset_import_pending"; return false;
      }
    } else {
      // A lost success response is safe to retry without re-uploading. Verify
      // actual installed bytes, rather than trusting ready metadata alone.
      ManagedAssetRecord current;
      if (!storage_path_exists(source.c_str()) && read_managed_asset_record(r.id.c_str(), current) &&
          current.ready && same_request(current.request, r) &&
          verify_download_file(r, managed_asset_path(r.id.c_str()), error)) return true;
      if (!storage_pool_admit(0, true, error)) return false;
      intent = {{r, false}, source};
      if (!internal::save_managed_asset_import_intent(r.id.c_str(), intent)) {
        error = "asset_import_intent_failed"; return false;
      }
      if (!admit_mutation(error) || !storage_move_file(source.c_str(),
          (base(r.id.c_str()) + ".import.part").c_str(), &error)) {
        (void)storage_delete_file(journal.c_str()); return false;
      }
    }
    if (!finish_import(intent, error)) return false;
    ++generations[index];
    std::lock_guard<std::recursive_mutex> guard(mutex);
    slots[index].error.clear(); slots[index].downloaded_bytes = r.bytes;
    slots[index].activation_pending = true;
    return true;
  }
  if (storage_path_exists(journal.c_str())) { error = "asset_import_pending"; return false; }
  if (!storage_pool_admit(r.bytes, true, error)) return false;
  const auto path = managed_asset_path(r.id.c_str());
  const auto part = path + ".part";
  struct Source { const char *path; size_t bytes; } input{source.c_str(), r.bytes};
  auto copy = [](StorageFileChunkWriter writer, void *target, void *opaque,
                 std::string &failure) {
    const auto &input = *static_cast<Source *>(opaque);
    struct Copy { StorageFileChunkWriter write; void *target; size_t remaining; } output{writer, target, input.bytes};
    const bool copied = storage_stream_file(input.path,
        [](const uint8_t *data, size_t size, void *opaque) {
          auto &output = *static_cast<Copy *>(opaque);
          std::string busy_error;
          if(!admit_mutation(busy_error))return false;
          if (size > output.remaining) return false;
          output.remaining -= size;
          return output.write(data, size, output.target);
        }, &output, &failure);
    return copied && output.remaining == 0;
  };
  if (!storage_write_streamed_file(part.c_str(), copy,
      &input, nullptr, &error)) return false;
  if (!verify_download_file(r, part, error)) {
    (void)storage_delete_file(part.c_str()); return false;
  }
  // Verify before changing publication. A bad digest/format never disturbs a
  // working asset; completed bytes are then published with a durable intent.
  ManagedAssetRecord record{r, false};
  if(!publish_verified(record,part,error))return false;
  ++generations[index];
  std::lock_guard<std::recursive_mutex> guard(mutex);
  slots[index].error.clear();
  slots[index].downloaded_bytes = r.bytes;
  slots[index].activation_pending = true;
  return true;
}
bool resume_managed_assets() {
  std::lock_guard<std::recursive_mutex> guard(mutex);
  catalog_frozen = true;
  if (!storage_pool_enabled()) return true;
  std::string mutation_error;
  if(!admit_mutation(mutation_error))return false;
  bool complete = true;
  for (size_t i = 0; i < asset_count; ++i) {
    if(!admit_mutation(mutation_error))return false;
    std::unique_lock<std::mutex> work(transactions[i],std::try_to_lock);
    if(!work.owns_lock()){complete=false;continue;}
    if (storage_path_exists((base(ids[i]) + ".import.json").c_str())) {
      internal::ManagedAssetImportIntent intent;
      if (!internal::read_managed_asset_import_intent(ids[i], intent)) {
        slots[i].error = "asset_import_intent_invalid"; complete = false;
      } else if (finish_import(intent, slots[i].error)) {
        ++generations[i]; slots[i].error.clear();
        slots[i].downloaded_bytes = intent.record.request.bytes;
        slots[i].activation_pending = true;
      } else complete = false;
      continue;
    }
    ManagedAssetRecord record;
    const bool pending = internal::read_pending_managed_asset_record(ids[i],record);
    if (!pending && storage_path_exists((base(ids[i])+".pending.json").c_str())) {
      slots[i].error="asset_pending_record_invalid"; complete=false; continue;
    }
    if(pending) {
      if (record.source_rejected) {
        slots[i].error=source_rejection_error(record);
        slots[i].rejected_request=record.request;
        slots[i].source_rejection=slots[i].error;
        continue; // Recovered terminal status needs a refreshed trusted source.
      }
      if (!slots[i].source_rejection.empty() && same_request(slots[i].rejected_request,record.request)) continue;
      const auto path=managed_asset_path(ids[i]);
      if(record.ready&&valid(record.request,false)&&verify_download_file(record.request,path,slots[i].error)) {
        if(!admit_mutation(mutation_error))return false;
        if(save_managed_asset_record(ids[i],record)) {
          (void)storage_delete_file((base(ids[i])+".pending.json").c_str());
          ++generations[i];
        } else { slots[i].error="asset_publication_failed"; complete=false; }
      } else if(record.ready&&valid(record.request,false)&&
          verify_download_file(record.request,path+".part",slots[i].error)) {
        if(publish_verified(record,path+".part",slots[i].error))++generations[i];
        else complete=false;
      } else if(valid(record.request)&&!network_job_active(slots[i].job)) {
        if(!queue(i,slots[i].error))complete=false;
      } else if(record.ready) {
        complete=false;
      }
      continue;
    }
    if (!read_managed_asset_record(ids[i], record) || record.ready) continue;
    // Factory publications have no download URL. A format-support upgrade may
    // recover them only after rechecking the actual file, digest and structure.
    if (record.request.url.empty() && valid(record.request, false)) {
      if (verify_download_file(record.request, managed_asset_path(ids[i]), slots[i].error)) {
        if(!admit_mutation(mutation_error))return false;
        record.ready = true;
        if (save_managed_asset_record(ids[i], record)) ++generations[i];
        else { slots[i].error = "asset_record_save_failed"; complete=false; }
      }
      continue;
    }
    if (
        valid(record.request) && !network_job_active(slots[i].job)) {
      if(!internal::save_pending_managed_asset_record(ids[i],record) ||
          !queue(i, slots[i].error))complete=false;
    }
  }
  return complete;
}
ManagedAssetInventorySnapshot managed_assets_inventory_snapshot() {
  std::lock_guard<std::recursive_mutex> guard(mutex);
  ManagedAssetInventorySnapshot snapshot;
  snapshot.pool_enabled = storage_pool_enabled();
  snapshot.asset_count = asset_count;
  for (size_t i = 0; i < asset_count; ++i) {
    auto &item = snapshot.assets[i];
    item.id = ids[i];
    item.model_families = model_families[i];
    item.model_family_count = model_family_counts[i];
    ManagedAssetRecord record;
    bool present = read_managed_asset_record(ids[i], record);
    if(!present)present=internal::read_pending_managed_asset_record(ids[i],record);
    item.state = !present ? ManagedAssetState::Absent :
        record.ready ? ManagedAssetState::Ready : ManagedAssetState::Invalid;
    item.downloading = network_job_active(slots[i].job);
    item.error = slots[i].error;
    item.bytes = record.request.bytes;
    item.downloaded_bytes = slots[i].downloaded_bytes;
    item.progress_percent = item.state == ManagedAssetState::Ready ? 100 :
        item.bytes ? static_cast<uint8_t>(std::min<size_t>(99,
            item.downloaded_bytes * 100 / item.bytes)) : 0;
    item.activation_pending = slots[i].activation_pending;
    item.revision = record.request.revision;
    item.sha256 = record.request.sha256;
  }
  return snapshot;
}
ManagedAssetReadinessSnapshot managed_assets_readiness_snapshot() {
  const auto inventory = managed_assets_inventory_snapshot();
  ManagedAssetReadinessSnapshot result;
  if (inventory.asset_count == 0) return result;
  if (!inventory.pool_enabled) {
    result.state = ManagedAssetReadinessState::StorageUnavailable;
    return result;
  }
  bool missing = false;
  bool failed = false;
  bool queued = false;
  bool activation_pending = false;
  for (size_t index = 0; index < inventory.asset_count; ++index) {
    const auto &asset = inventory.assets[index];
    if (asset.state == ManagedAssetState::Absent) {
      missing = true;
      continue;
    }
    if (asset.downloading) {
      if (asset.progress_percent > 0) {
        result.state = ManagedAssetReadinessState::Downloading;
        result.progress_percent = asset.progress_percent;
        return result;
      }
      if (!asset.error.empty()) failed = true;
      else queued = true;
      continue;
    }
    if (asset.state == ManagedAssetState::Invalid) {
      if (!asset.error.empty()) failed = true;
      else missing = true;
      continue;
    }
    activation_pending |= asset.activation_pending;
  }
  result.state = failed ? ManagedAssetReadinessState::Failed :
      queued ? ManagedAssetReadinessState::Queued :
      missing ? ManagedAssetReadinessState::Missing :
      activation_pending ? ManagedAssetReadinessState::RestartRequired :
      ManagedAssetReadinessState::Ready;
  return result;
}
ManagedAssetCatalogSnapshot managed_assets_snapshot() {
  std::lock_guard<std::recursive_mutex> guard(mutex);
  const auto inventory = managed_assets_inventory_snapshot();
  ManagedAssetCatalogSnapshot snapshot;
  snapshot.pool_enabled = inventory.pool_enabled;
  snapshot.asset_count = inventory.asset_count;
  snapshot.assets = inventory.assets;
  snapshot.user_reserve_bytes = storage_pool_user_reserve();
  snapshot.headroom_bytes = storage_pool_headroom(storage_total_bytes());
  snapshot.total_bytes = storage_total_bytes();
  snapshot.used_bytes = storage_used_bytes();
  snapshot.user_used_bytes = storage_pool_user_used();
  return snapshot;
}
bool managed_asset_storage_enabled() { return storage_pool_enabled(); }
bool managed_asset_ready(const char *id) {
  if (!id || index_of(id) < 0) return false;
  ManagedAssetRecord record;
  return read_managed_asset_record(id, record) && record.ready && valid(record.request, false);
}
uint32_t managed_asset_generation(const char *id) {
  const int index = id ? index_of(id) : -1;
  return index < 0 ? 0 : generations[index].load();
}
bool validate_managed_asset(const char *id,std::string &error) {
  error.clear();
  const int index=id?index_of(id):-1;
  if(index<0){error="asset_id_invalid";return false;}
  std::lock_guard<std::mutex> work(transactions[index]);
  ManagedAssetRecord record;
  if(!read_managed_asset_record(id,record)||!record.ready){error="asset_not_ready";return false;}
  if(!valid(record.request,false)){error="asset_record_invalid";return false;}
  return verify_download_file(record.request,managed_asset_path(id),error);
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
  auto *data = read_verified(request, managed_asset_path(id), bytes, error);
  // Allocation failure is transient and must not invalidate a good publication.
  if (!data && error == "asset_verification_failed") {
    std::string mutation_error;
    if(admit_mutation(mutation_error)) {
      record.ready=false;(void)save_managed_asset_record(id,record);++generations[index_of(id)];
    }
  }
  return data;
}
} // namespace lunaredu::lunaros
