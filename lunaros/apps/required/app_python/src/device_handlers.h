#pragma once

#include <string>

#include <ArduinoJson.h>

#include "device_api/device_operation_types.h"
#include "lunaros_micropython_runtime.h"

using lunaredu::lunaros::DeviceOperationResult;

void fill_micropython_capabilities_payload(
    lunaredu::lunaros::DeviceApiContext &device, JsonObject payload);
lunaredu::lunaros::DeviceOperationResult fill_micropython_runtime_status_payload(
    lunaredu::lunaros::DeviceApiContext &device, JsonObject payload);
lunaredu::lunaros::DeviceOperationResult handle_micropython_runtime_enter(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload);
lunaredu::lunaros::DeviceOperationResult handle_micropython_runtime_leave(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload);
lunaredu::lunaros::DeviceOperationResult handle_micropython_repl_enter(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload);
lunaredu::lunaros::DeviceOperationResult handle_micropython_repl_leave(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload);
lunaredu::lunaros::DeviceOperationResult handle_micropython_repl_feed(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload);
lunaredu::lunaros::DeviceOperationResult fill_micropython_runtime_logs_payload(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload);
lunaredu::lunaros::DeviceOperationResult fill_micropython_script_list_payload(
    lunaredu::lunaros::DeviceApiContext &device, JsonObject payload);
lunaredu::lunaros::DeviceOperationResult handle_micropython_script_run(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload);
lunaredu::lunaros::DeviceOperationResult handle_micropython_script_stop(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload);
lunaredu::lunaros::DeviceOperationResult handle_micropython_script_delete(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload);
lunaredu::lunaros::DeviceOperationResult handle_micropython_script_autorun_set(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload);
lunaredu::lunaros::DeviceOperationResult
handle_micropython_script_upload_bulk_payload(
    lunaredu::lunaros::DeviceApiContext &device,
    const lunaredu::lunaros::DeviceBulkRequest &request,
    JsonObject response_payload);
lunaredu::lunaros::DeviceOperationResult
handle_micropython_project_upload_bulk_payload(
    lunaredu::lunaros::DeviceApiContext &device,
    const lunaredu::lunaros::DeviceBulkRequest &request,
    JsonObject response_payload);
lunaredu::lunaros::DeviceOperationResult handle_micropython_project_preview(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload,
    JsonObject response_payload);
lunaredu::lunaros::DeviceOperationResult handle_micropython_project_run(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload,
    JsonObject response_payload);
lunaredu::lunaros::DeviceOperationResult handle_micropython_project_surface_get(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload,
    JsonObject response_payload);
lunaredu::lunaros::DeviceOperationResult handle_micropython_project_action_invoke(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload,
    JsonObject response_payload);
lunaredu::lunaros::DeviceOperationResult fill_micropython_project_list_payload(
    lunaredu::lunaros::DeviceApiContext &device, JsonObject response_payload);
lunaredu::lunaros::DeviceOperationResult handle_micropython_project_rollback(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload,
    JsonObject response_payload);
lunaredu::lunaros::DeviceOperationResult handle_micropython_project_delete(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload,
    JsonObject response_payload);
void fill_micropython_runtime_status_payload(
    JsonObject payload, const MicroPythonRuntimeStatus &status);
