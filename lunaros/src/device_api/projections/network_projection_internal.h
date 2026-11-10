#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "lunaredu_lunaros/device_api/device_projection.h"
#include "lunaredu_lunaros/network/network_provisioning.h"
#include "lunaredu_lunaros/persistence/settings.h"

namespace lunaredu::lunaros {

void fill_network_service_status_payload(JsonObject payload);
void fill_network_service_bootstrap_payload(JsonObject payload);
void fill_network_config_payload(JsonObject payload);
DeviceOperationResult handle_network_config_set(
    JsonVariantConst request_payload, JsonObject response_payload);
DeviceOperationResult handle_network_config_clear(
    JsonVariantConst request_payload, JsonObject response_payload);
void fill_network_provisioning_status_payload(JsonObject payload);
DeviceOperationResult handle_network_provision_submit(
    JsonVariantConst request_payload, JsonObject response_payload);
DeviceOperationResult handle_network_provision_scan(
    JsonVariantConst request_payload, JsonObject response_payload);
bool setup_network_projection(std::string &error);

} // namespace lunaredu::lunaros
