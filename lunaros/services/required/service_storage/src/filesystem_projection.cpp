#include "device_api/device_operation_builders.h"
#include "device_api/device_api_context.h"
#include "lunaros_storage_projection.h"
#include "device_api/device_projection.h"
#include "lunaros_storage_device_operations.h"
#include "lunaros_storage.h"
#include "lunaros_storage_quota.h"

namespace lunaredu::lunaros {

namespace {

DeviceOperationResult set_pool_quota_request([[maybe_unused]] DeviceApiContext &device, const DeviceBulkRequest &request, JsonObject payload) {
  std::string error;
  if (!request.payload["user_reserve_bytes"].is<size_t>() ||
      !storage_pool_set_user_reserve(request.payload["user_reserve_bytes"].as<size_t>(), error))
    return device_operation_error(error.empty() ? "invalid_user_reserve" : error.c_str(), 409, "quota_update_failed");
  payload["user_reserve_bytes"] = storage_pool_user_reserve();
  return device_operation_ack();
}

void fill_storage_payload([[maybe_unused]] DeviceApiContext &device, JsonObject payload) {
  payload["ready"] = storage_ready();
  payload["type"] = "littlefs";
  payload["mount_path"] = storage_mount_path();
  payload["partition_label"] = storage_partition_label();
  payload["total_bytes"] =
      static_cast<uint64_t>(storage_total_bytes());
  payload["used_bytes"] = static_cast<uint64_t>(storage_used_bytes());
  payload["last_error"] = storage_last_error();
}

void fill_filesystem_service_status_payload([[maybe_unused]] DeviceApiContext &device, JsonObject payload) {
  payload["ready"] = storage_ready();

  JsonObject volumes = payload["volumes"].to<JsonObject>();
  fill_storage_payload(device, volumes["littlefs"].to<JsonObject>());
}

void fill_filesystem_device_payload([[maybe_unused]] DeviceApiContext &device, JsonObject payload) {
  payload["filesystem"]["ready"] = storage_ready();
}

constexpr DeviceOperationDefinition kLittleFsOperations[] = {
    device_operation_on_caller(make_device_bulk_write("quota.set", set_pool_quota_request)),
    device_operation_remote(make_device_json_provider("status.get", fill_storage_payload,
                              kNetworkIoServiceExecutor), DeviceRemoteAccess::Query),
    device_operation_on_caller(make_device_bulk_read(
        "list", handle_littlefs_bulk_list_request)),
    device_operation_on_caller(make_device_bulk_read(
        "read", handle_littlefs_bulk_read_request)),
    device_operation_on_caller(make_device_bulk_write(
        "mkdir", handle_littlefs_bulk_mkdir_request)),
    device_operation_on_caller(make_device_bulk_write(
        "write", handle_littlefs_bulk_write_request,
        device_operation_body_limit(kDeviceDocumentBodyBytes))),
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

DeviceOperationsView storage_device_operations() {
  return {kLittleFsOperations, std::size(kLittleFsOperations)};
}
const DeviceProjectionContribution &storage_device_projection(){
  static constexpr DeviceMetadataProjection metadata={"storage.filesystem","filesystem",fill_filesystem_service_status_payload,fill_filesystem_device_payload};
  static const DeviceProjectionContribution contribution{&storage_service_capability_descriptor(),&kLittleFsProjection,&metadata,false};
  return contribution;
}
}
