#pragma once
#include "lunaredu_lunaros/delivery/managed_assets.h"

namespace lunaredu::lunaros::internal {
// On-disk publication metadata. The codec remains private to Delivery.
struct ManagedAssetRecord {
  ManagedAssetRequest request;
  bool ready = false;
};
bool read_managed_asset_record(const char *id, ManagedAssetRecord &record);
bool save_managed_asset_record(const char *id, const ManagedAssetRecord &record);
} // namespace lunaredu::lunaros::internal
