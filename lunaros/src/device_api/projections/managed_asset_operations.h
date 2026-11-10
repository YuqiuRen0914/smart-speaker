#pragma once
#include "lunaredu_lunaros/device_api/device_projection.h"
#include "lunaredu_lunaros/device_api/device_projection_json.h"
#include "lunaredu_lunaros/delivery/managed_assets.h"
#include "lunaredu_lunaros/persistence/storage_pool.h"

namespace lunaredu::lunaros {
inline DeviceOperationResult install_asset_request(const DeviceBulkRequest &request, JsonObject payload) {
  const auto &p = request.payload;
  ManagedAssetRequest asset{device_operation_string(p["id"]), device_operation_string(p["revision"]),
      device_operation_string(p["url"]), device_operation_string(p["sha256"]), p["bytes"].as<size_t>()};
  std::string error;
  if (!request_managed_asset(asset, error)) return device_operation_error(error.c_str(), 409, "asset_install_failed");
  payload["queued"] = true; payload["activation"] = "restart";
  return device_operation_ack();
}
inline DeviceOperationResult remove_asset_request(const DeviceBulkRequest &request, JsonObject payload) {
  std::string error;
  if (!remove_managed_asset(device_operation_string(request.payload["id"]), error))
    return device_operation_error(error.c_str(), 409, "asset_remove_failed");
  payload["removed"] = true;
  return device_operation_ack();
}
inline DeviceOperationResult set_pool_quota_request(const DeviceBulkRequest &request, JsonObject payload) {
  std::string error;
  if (!request.payload["user_reserve_bytes"].is<size_t>() ||
      !storage_pool_set_user_reserve(request.payload["user_reserve_bytes"].as<size_t>(), error))
    return device_operation_error(error.empty() ? "invalid_user_reserve" : error.c_str(), 409, "quota_update_failed");
  payload["user_reserve_bytes"] = storage_pool_user_reserve();
  return device_operation_ack();
}
} // namespace lunaredu::lunaros
