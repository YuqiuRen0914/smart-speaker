#include "device_api/device_operation_builders.h"
#include "lunaros_resource_projection.h"
#include "lunaros_resource_managed_assets.h"
#include "lunaros_storage_quota.h"
#include "device_api/device_api_context.h"
namespace lunaredu::lunaros {
namespace {
DeviceOperationResult install_asset_request([[maybe_unused]] DeviceApiContext &device, const DeviceBulkRequest &request, JsonObject payload) {
  const auto &p = request.payload;
  ManagedAssetRequest asset{device_operation_string(p["id"]), device_operation_string(p["revision"]),
      device_operation_string(p["url"]), device_operation_string(p["sha256"]), p["bytes"].as<size_t>()};
  std::string error;
  if (!request_managed_asset(asset, error)) return device_operation_error(error.c_str(), 409, "asset_install_failed");
  payload["queued"] = true; payload["activation"] = "restart";
  return device_operation_ack();
}
DeviceOperationResult cancel_asset_request([[maybe_unused]] DeviceApiContext &, const DeviceBulkRequest &request, JsonObject payload) {
  std::string error;
  if (!cancel_managed_asset_update(device_operation_string(request.payload["id"]), error))
    return device_operation_error(error.c_str(), 409, "asset_cancel_failed");
  payload["cancelled"] = true;
  return device_operation_ack();
}
DeviceOperationResult remove_asset_request([[maybe_unused]] DeviceApiContext &device, const DeviceBulkRequest &request, JsonObject payload) {
  std::string error;
  if (!remove_managed_asset(device_operation_string(request.payload["id"]), error))
    return device_operation_error(error.c_str(), 409, "asset_remove_failed");
  payload["removed"] = true;
  return device_operation_ack();
}
DeviceOperationResult import_asset_request([[maybe_unused]] DeviceApiContext &, const DeviceBulkRequest &request, JsonObject payload) {
  const auto &p = request.payload;
  ManagedAssetRequest asset{device_operation_string(p["id"]), device_operation_string(p["revision"]),
      "", device_operation_string(p["sha256"]), p["bytes"].as<size_t>()};
  const auto consume = p["consume_source"];
  if (!consume.isUnbound() && !consume.is<bool>())
    return device_operation_error("asset_import_invalid", 400, "asset_import_failed");
  const bool consume_source = consume.as<bool>();
  std::string error;
  if (!import_managed_asset(asset, device_operation_string(p["path"]), error, consume_source))
    return device_operation_error(error.c_str(), 409, "asset_import_failed");
  payload["imported"] = true; payload["activation"] = "restart";
  payload["source_consumed"] = consume_source;
  return device_operation_ack();
}
void fill_managed_assets_payload([[maybe_unused]] DeviceApiContext &device, JsonObject payload) {
  const auto snapshot = managed_assets_snapshot();
  payload["pool_enabled"] = snapshot.pool_enabled;
  payload["user_reserve_bytes"] = snapshot.user_reserve_bytes;
  payload["headroom_bytes"] = snapshot.headroom_bytes;
  payload["total_bytes"] = snapshot.total_bytes;
  payload["used_bytes"] = snapshot.used_bytes;
  payload["user_used_bytes"] = snapshot.user_used_bytes;
  auto items = payload["assets"].to<JsonArray>();
  for (size_t i = 0; i < snapshot.asset_count; ++i) {
    const auto &asset = snapshot.assets[i];
    auto item = items.add<JsonObject>();
    item["id"] = asset.id;
    item["state"] = asset.state == ManagedAssetState::Absent ? "absent" :
        asset.state == ManagedAssetState::Ready ? "ready" : "invalid";
    item["downloading"] = asset.downloading;
    item["error"] = asset.error;
    item["bytes"] = asset.bytes;
    item["downloaded_bytes"] = asset.downloaded_bytes;
    item["progress_percent"] = asset.progress_percent;
    item["activation_pending"] = asset.activation_pending;
    item["revision"] = asset.revision;
    item["sha256"] = asset.sha256;
    item["activation"] = "restart";
    if (asset.model_family_count) {
      auto families = item["model_families"].to<JsonArray>();
      for (size_t f = 0; f < asset.model_family_count; ++f) families.add(asset.model_families[f]);
    }
  }
}


constexpr DeviceOperationDefinition operations[]={
    device_operation_remote(device_operation_on_caller(make_device_json_provider("assets.get", fill_managed_assets_payload)), DeviceRemoteAccess::Query),
    device_operation_on_caller(make_device_bulk_write("assets.install", install_asset_request)),
    device_operation_on_caller(make_device_bulk_write("assets.cancel", cancel_asset_request)),
    device_operation_on_caller(make_device_bulk_write("assets.import", import_asset_request)),
    device_operation_on_caller(make_device_bulk_write("assets.remove", remove_asset_request)),
};
}
DeviceOperationsView resource_device_operations(){return {operations,std::size(operations)};}
}
