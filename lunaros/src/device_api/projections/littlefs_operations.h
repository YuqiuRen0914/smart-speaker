#pragma once

#include <ArduinoJson.h>

#include "lunaredu_lunaros/device_api/device_projection.h"

namespace lunaredu::lunaros {

DeviceOperationResult handle_littlefs_bulk_list_request(
    const DeviceBulkRequest &request, JsonObject payload);
DeviceOperationResult handle_littlefs_bulk_read_request(
    const DeviceBulkRequest &request, JsonObject payload);
DeviceOperationResult handle_littlefs_bulk_write_request(
    const DeviceBulkRequest &request, JsonObject payload);
DeviceOperationResult handle_littlefs_bulk_upload_request(
    const DeviceBulkRequest &request, JsonObject payload);
DeviceOperationResult handle_littlefs_bulk_delete_request(
    const DeviceBulkRequest &request, JsonObject payload);
DeviceOperationResult handle_littlefs_bulk_mkdir_request(
    const DeviceBulkRequest &request, JsonObject payload);
DeviceOperationResult handle_littlefs_bulk_format_request(
    const DeviceBulkRequest &request, JsonObject payload);
DeviceOperationResult handle_littlefs_media_file_request(
    const DeviceMediaRequest &request, const DeviceMediaWriter &writer);

} // namespace lunaredu::lunaros
