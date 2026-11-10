#include "lunaredu_lunaros/device_api/standard_projections.h"
#include "lunaredu_lunaros/device_api/device_projection.h"
#include "device_api/projections/littlefs_operations.h"
#include "lunaredu_lunaros/persistence/storage.h"
#include "device_api/projections/managed_asset_operations.h"

namespace lunaredu::lunaros {

namespace {

void fill_managed_assets_payload(JsonObject payload) {
  const auto snapshot = managed_assets_snapshot();
  payload["pool_enabled"] = snapshot.pool_enabled;
  payload["user_reserve_bytes"] = snapshot.user_reserve_bytes;
  payload["headroom_bytes"] = snapshot.headroom_bytes;
  payload["total_bytes"] = snapshot.total_bytes;
  payload["used_bytes"] = snapshot.used_bytes;
  payload["user_used_bytes"] = snapshot.user_used_bytes;
  auto items = payload["assets"].to<JsonArray>();
  for (const auto &asset : snapshot.assets) {
    auto item = items.add<JsonObject>();
    item["id"] = asset.id;
    item["state"] = asset.state == ManagedAssetState::Absent ? "absent" :
        asset.state == ManagedAssetState::Ready ? "ready" : "invalid";
    item["downloading"] = asset.downloading;
    item["error"] = asset.error;
    item["bytes"] = asset.bytes;
    item["revision"] = asset.revision;
    item["activation"] = "restart";
  }
}

void fill_storage_payload(JsonObject payload) {
  payload["ready"] = storage_ready();
  payload["type"] = "littlefs";
  payload["mount_path"] = storage_mount_path();
  payload["partition_label"] = storage_partition_label();
  payload["total_bytes"] =
      static_cast<uint64_t>(storage_total_bytes());
  payload["used_bytes"] = static_cast<uint64_t>(storage_used_bytes());
  payload["last_error"] = storage_last_error();
}

void fill_filesystem_service_status_payload(JsonObject payload) {
  payload["ready"] = storage_ready();

  JsonObject volumes = payload["volumes"].to<JsonObject>();
  fill_storage_payload(volumes["littlefs"].to<JsonObject>());
}

void fill_filesystem_device_payload(JsonObject payload) {
  payload["filesystem"]["ready"] = storage_ready();
}

constexpr DeviceOperationDefinition kLittleFsOperations[] = {
    device_operation_on_caller(make_device_json_provider("assets.get", fill_managed_assets_payload)),
    device_operation_on_caller(make_device_bulk_write("assets.install", install_asset_request)),
    device_operation_on_caller(make_device_bulk_write("assets.remove", remove_asset_request)),
    device_operation_on_caller(make_device_bulk_write("quota.set", set_pool_quota_request)),
    make_device_json_provider("status.get", fill_storage_payload,
                              kNetworkIoServiceExecutor),
    device_operation_on_caller(make_device_bulk_read(
        "list", handle_littlefs_bulk_list_request)),
    device_operation_on_caller(make_device_bulk_read(
        "read", handle_littlefs_bulk_read_request)),
    device_operation_on_caller(make_device_bulk_write(
        "mkdir", handle_littlefs_bulk_mkdir_request)),
    device_operation_on_caller(make_device_bulk_write(
        "write", handle_littlefs_bulk_write_request)),
    device_operation_on_caller(make_device_upload(
        "upload", handle_littlefs_bulk_upload_request)),
    device_operation_on_caller(make_device_bulk_write(
        "delete", handle_littlefs_bulk_delete_request,
        device_operation_method(DeviceOperationMethod::Delete))),
    device_operation_on_caller(make_device_bulk_write(
        "format", handle_littlefs_bulk_format_request)),
    device_operation_on_caller(make_device_media(
        "file.get", handle_littlefs_media_file_request)),
};

const DeviceModuleProjectionMetadata kLittleFsModule = {
    "filesystem",
    "filesystem",
};

const DeviceModuleProjection kLittleFsProjection = {
    "littlefs_service",
    &kLittleFsModule,
    kLittleFsOperations,
    sizeof(kLittleFsOperations) / sizeof(kLittleFsOperations[0]),
};

} // namespace

bool setup_storage_projection(std::string &error) {
  static constexpr DeviceMetadataProjection kFilesystemMetadata = {
      "storage.filesystem", "filesystem",
      fill_filesystem_service_status_payload, fill_filesystem_device_payload};
  return register_device_projection(
      {&storage_service_capability_descriptor(), &kLittleFsProjection,
       &kFilesystemMetadata, false},
      &error);
}

} // namespace lunaredu::lunaros
