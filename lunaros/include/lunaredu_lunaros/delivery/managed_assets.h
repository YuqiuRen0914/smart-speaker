#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <array>

namespace lunaredu::lunaros {
// These descriptors must come from an authenticated administrator or a trusted
// application manifest. The URL is fetched with mandatory TLS verification.
struct ManagedAssetRequest {
  std::string id;
  std::string revision;
  std::string url;
  std::string sha256;
  size_t bytes = 0;
};
bool request_managed_asset(const ManagedAssetRequest &request, std::string &error);
bool remove_managed_asset(const std::string &id, std::string &error);
void resume_managed_assets();
enum class ManagedAssetState { Absent, Ready, Invalid };
inline constexpr size_t kManagedAssetCount = 6;
struct ManagedAssetSnapshot {
  std::string id;
  ManagedAssetState state = ManagedAssetState::Absent;
  bool downloading = false;
  std::string error;
  size_t bytes = 0;
  std::string revision;
};
struct ManagedAssetCatalogSnapshot {
  bool pool_enabled = false;
  size_t user_reserve_bytes = 0;
  size_t headroom_bytes = 0;
  size_t total_bytes = 0;
  size_t used_bytes = 0;
  size_t user_used_bytes = 0;
  std::array<ManagedAssetSnapshot, kManagedAssetCount> assets;
};
ManagedAssetCatalogSnapshot managed_assets_snapshot();
// Verifies persisted publication and content before allocating explicit PSRAM.
// The caller owns the returned allocation and must heap_caps_free it only
// after all font/model objects referencing it have been destroyed.
void *load_managed_asset(const char *id, size_t &bytes, std::string &error);
bool managed_asset_ready(const char *id);
// Allocation-free invalidation signal for running consumers.
uint32_t managed_asset_generation(const char *id);
std::string managed_asset_path(const char *id);
} // namespace lunaredu::lunaros
