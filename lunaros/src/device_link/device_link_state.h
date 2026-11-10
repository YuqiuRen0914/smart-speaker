#pragma once

#include <string>

#include <ArduinoJson.h>

#include "lunaredu_lunaros/device_link/device_link.h"

namespace lunaredu::lunaros::device_link {

void configure_runtime(const DeviceLinkConfig &config);
const DeviceLinkConfig &runtime_config();

void update_pairing_snapshot(DeviceLinkBindingState state,
                             const std::string &code = {},
                             const std::string &bind_url = {},
                             int64_t expires_at_ms = 0,
                             const std::string &error = {},
                             int binding_epoch = -1,
                             const std::string &account_id = {},
                             const std::string &account_display_name = {},
                             const std::string &account_avatar_url = {},
                             bool account_can_manage_hardware = false);
void replace_pairing_snapshot(DeviceLinkPairingSnapshot snapshot);
void load_cached_binding();
void persist_cached_binding();
void erase_cached_binding();
DeviceLinkPairingSnapshot pairing_snapshot();
// Allocation-free scheduler query; full snapshots remain available to readers
// that need account/resource metadata from the same revision.
DeviceLinkBindingState pairing_state();
void update_platform_connection(DeviceLinkPlatformConnectionState state,
                                int status_code = -1,
                                const std::string &error = {});
DeviceLinkPlatformConnectionSnapshot platform_connection_snapshot();

DeviceLinkBindingState state_from_server(const std::string &status);
bool account_identity_ready();
bool network_connected();
void report_state(bool operation_succeeded = true);
void add_capabilities(JsonArray capabilities);
void add_runtime_state(JsonObject state);

} // namespace lunaredu::lunaros::device_link
