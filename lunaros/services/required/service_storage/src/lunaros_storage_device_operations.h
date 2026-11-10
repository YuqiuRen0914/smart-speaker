#pragma once
#include "device_api/device_api_context.h"

#include <ArduinoJson.h>

#include "device_api/device_operation_types.h"

namespace lunaredu::lunaros {

const DeviceCapabilityDescriptor &storage_service_capability_descriptor();

DeviceOperationResult handle_littlefs_bulk_list_request([[maybe_unused]] DeviceApiContext &device, const DeviceBulkRequest &request, JsonObject payload);
DeviceOperationResult handle_littlefs_bulk_read_request([[maybe_unused]] DeviceApiContext &device, const DeviceBulkRequest &request, JsonObject payload);
DeviceOperationResult handle_littlefs_bulk_write_request([[maybe_unused]] DeviceApiContext &device, const DeviceBulkRequest &request, JsonObject payload);
DeviceOperationResult handle_littlefs_bulk_upload_request([[maybe_unused]] DeviceApiContext &device, const DeviceBulkRequest &request, JsonObject payload);
DeviceOperationResult handle_littlefs_bulk_delete_request([[maybe_unused]] DeviceApiContext &device, const DeviceBulkRequest &request, JsonObject payload);
DeviceOperationResult handle_littlefs_bulk_mkdir_request([[maybe_unused]] DeviceApiContext &device, const DeviceBulkRequest &request, JsonObject payload);
DeviceOperationResult handle_littlefs_bulk_format_request([[maybe_unused]] DeviceApiContext &device, const DeviceBulkRequest &request, JsonObject payload);
DeviceOperationResult handle_littlefs_media_file_request([[maybe_unused]] DeviceApiContext &device, const DeviceMediaRequest &request, const DeviceMediaWriter &writer);

} // namespace lunaredu::lunaros
