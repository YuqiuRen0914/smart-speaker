#pragma once
#include "lunaros_resource_managed_assets.h"

namespace lunaredu::lunaros::internal {
// On-disk publication metadata. The codec remains private to Delivery.
struct ManagedAssetRecord {
  ManagedAssetRequest request;
  bool ready = false;
  // Optional in schema 1; old publications/pending records omit these fields.
  bool source_rejected = false;
  std::string source_error{};
  int source_http_status = 0;
};
bool read_managed_asset_record(const char *id, ManagedAssetRecord &record);
bool save_managed_asset_record(const char *id, const ManagedAssetRecord &record);
bool read_pending_managed_asset_record(const char *id, ManagedAssetRecord &record);
bool save_pending_managed_asset_record(const char *id, const ManagedAssetRecord &record);
// Private consume-source intent; ready means captured bytes passed validation.
struct ManagedAssetImportIntent {
  ManagedAssetRecord record;
  std::string source;
};
bool read_managed_asset_import_intent(const char *id, ManagedAssetImportIntent &intent);
bool save_managed_asset_import_intent(const char *id, const ManagedAssetImportIntent &intent);
} // namespace lunaredu::lunaros::internal
