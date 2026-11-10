#pragma once
#include "device_api/device_api_context.h"

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "device_api/device_operation_types.h"
#include "configuration/settings.h"

namespace lunaredu::lunaros {

void fill_network_wifi_status(DeviceApiContext &,JsonObject);
DeviceOperationResult handle_network_wifi_connect(DeviceApiContext &,JsonVariantConst,JsonObject);
DeviceOperationResult handle_network_wifi_cancel(DeviceApiContext &,JsonVariantConst,JsonObject);
DeviceOperationResult handle_network_wifi_forget(DeviceApiContext &,JsonVariantConst,JsonObject);
DeviceOperationResult handle_network_wifi_scan(DeviceApiContext &,JsonVariantConst,JsonObject);
void fill_network_service_status_payload([[maybe_unused]] DeviceApiContext &device, JsonObject payload);
void fill_network_service_bootstrap_payload([[maybe_unused]] DeviceApiContext &device, JsonObject payload);
void fill_network_config_payload([[maybe_unused]] DeviceApiContext &device, JsonObject payload);
DeviceOperationResult handle_network_config_set([[maybe_unused]] DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload);
DeviceOperationResult handle_network_config_clear([[maybe_unused]] DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload);

} // namespace lunaredu::lunaros
