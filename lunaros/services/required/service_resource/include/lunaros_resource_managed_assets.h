#pragma once
#include "lunaros_resource_manifest.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <array>

namespace lunaredu::lunaros {
// Product-owned, immutable boot manifest. Declare before starting services;
// omitted assets do not appear in this firmware's catalog. IDs are safe names,
// never filesystem paths. Descriptors/strings must live until reboot.
bool configure_managed_assets(const ManagedAssetDefinition *assets, size_t count,
                              std::string &error);
// Runtime supplies a coherent firmware-update predicate before services start.
// While updating or awaiting first-boot confirmation, resources remain intact.
// A missing predicate denies mutation; it does not prevent existing reads.
void set_managed_asset_update_busy_probe(bool (*busy)());
// Configure before services start. Recovery/installer firmware can retain the
// readable catalog without automatically replaying persisted publications or
// downloads. Explicit authorized requests/imports keep their usual admission.
void configure_managed_asset_startup_recovery(bool enabled);
// Main upkeep retries startup publication recovery after firmware confirmation.
// No new worker; completed restoration is idempotent until the next service start.
void poll_managed_asset_recovery();
// Nonblocking snapshot; maintenance first denies new operations, then waits
// until every active publication/read transaction has retired.
bool managed_asset_operations_idle();
// These descriptors must come from an authenticated administrator or a trusted
// application manifest. The URL is fetched with mandatory TLS verification.
struct ManagedAssetRequest {
  std::string id;
  std::string revision;
  std::string url;
  std::string sha256;
  size_t bytes = 0;
};
// Rejected HTTP/content sources need a new trusted descriptor; repeating the
// same rejected source fails across reboot. Rotating only its URL can update a
// queued descriptor without resetting that job's bounded attempt budget.
bool request_managed_asset(const ManagedAssetRequest &request, std::string &error);
// Retire a pending download without deleting the published resource. A running
// worker must acknowledge cancellation first; repeat only after it retires.
// The cancelled descriptor is retained so startup/cloud retries cannot replay it.
bool cancel_managed_asset_update(const std::string &id, std::string &error);
// Import a locally uploaded user file through the same digest/format checks.
// The manifest remains trusted management input; reserved paths are forbidden.
// Source files are preserved by default. Explicit consume_source atomically moves
// the file into private staging before verification; even a rejected source may
// be consumed. Retry the same request after an interrupted response. A completed
// publication activates after restart. Existing published bytes are never deleted
// to make room for a replacement.
bool import_managed_asset(const ManagedAssetRequest &request,
                          const std::string &source, std::string &error,
                          bool consume_source = false);
bool remove_managed_asset(const std::string &id, std::string &error);

enum class ManagedAssetState { Absent, Ready, Invalid };
inline constexpr size_t kManagedAssetCapacity = 6;
struct ManagedAssetSnapshot {
  std::string id;
  ManagedAssetState state = ManagedAssetState::Absent;
  bool downloading = false;
  std::string error;
  size_t bytes = 0;
  size_t downloaded_bytes = 0;
  uint8_t progress_percent = 0;
  bool activation_pending = false;
  std::string revision;
  std::string sha256;
  const char *const *model_families = nullptr;
  size_t model_family_count = 0;
};
struct ManagedAssetCatalogSnapshot {
  bool pool_enabled = false;
  size_t user_reserve_bytes = 0;
  size_t headroom_bytes = 0;
  size_t total_bytes = 0;
  size_t used_bytes = 0;
  size_t user_used_bytes = 0;
  size_t asset_count = 0;
  std::array<ManagedAssetSnapshot, kManagedAssetCapacity> assets;
};
// Presence updates need only installed revisions; avoid a filesystem usage
// walk on each network heartbeat.
struct ManagedAssetInventorySnapshot {
  bool pool_enabled = false;
  size_t asset_count = 0;
  std::array<ManagedAssetSnapshot, kManagedAssetCapacity> assets;
};
ManagedAssetInventorySnapshot managed_assets_inventory_snapshot();
enum class ManagedAssetReadinessState : uint8_t {
  Ready, Missing, Queued, Downloading, Failed, RestartRequired,
  StorageUnavailable,
};
struct ManagedAssetReadinessSnapshot {
  ManagedAssetReadinessState state = ManagedAssetReadinessState::Ready;
  uint8_t progress_percent = 0;
};
// Aggregate the product's configured asset catalog without UI-specific text.
ManagedAssetReadinessSnapshot managed_assets_readiness_snapshot();
ManagedAssetCatalogSnapshot managed_assets_snapshot();
// Whether this firmware/storage layout supports the managed asset catalog.
// Callers must not depend on the persistence pool's size or path policy.
bool managed_asset_storage_enabled();
// Verifies persisted publication and content before allocating explicit PSRAM.
// The caller owns the returned allocation and must heap_caps_free it only
// after all font/model objects referencing it have been destroyed.
void *load_managed_asset(const char *id, size_t &bytes, std::string &error);
// Blocking management/startup validation through Resource's existing streaming
// digest and format/family verifier. Uncompressed assets need no full PSRAM
// allocation. Does not request downloads, complete firmware or activate Apps.
bool validate_managed_asset(const char *id,std::string &error);
// Validates a declared set and rejects a publication changed during the walk.
// On failure, failed_asset names the member; errors remain protocol neutral.
bool validate_managed_assets(const char *const *ids,size_t count,
    std::string &failed_asset,std::string &error);
bool managed_asset_ready(const char *id);
// Allocation-free invalidation signal for running consumers.
uint32_t managed_asset_generation(const char *id);
std::string managed_asset_path(const char *id);
} // namespace lunaredu::lunaros
