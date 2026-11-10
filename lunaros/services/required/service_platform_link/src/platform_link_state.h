#pragma once

#include <string>

#include <ArduinoJson.h>

#include "lunaros_platform_link.h"

namespace lunaredu::lunaros::platform_link {

void configure_runtime(DeviceApiContext &runtime, const PlatformLinkConfig &config);
DeviceApiContext *runtime_device();
const PlatformLinkConfig &runtime_config();

void update_pairing_snapshot(PlatformLinkBindingState state,
                             const std::string &code = {},
                             const std::string &bind_url = {},
                             int64_t expires_at_ms = 0,
                             const std::string &error = {},
                             int binding_epoch = -1,
                             const std::string &account_id = {},
                             const std::string &account_display_name = {},
                             const std::string &account_avatar_url = {},
                             bool account_can_manage_hardware = false);
void update_shared_access(int epoch, bool enabled, const std::string &manager = {});
void replace_pairing_snapshot(PlatformLinkPairingSnapshot snapshot);
void update_account_avatar_resource(const PlatformLinkPairingSnapshot &binding,
                                    JsonVariantConst manifest);
bool read_cached_binding(PlatformLinkPairingSnapshot &snapshot, std::string &error,
                         bool *authority_restored = nullptr,bool *local_unbound = nullptr);
bool persist_cached_binding(const PlatformLinkPairingSnapshot &snapshot, std::string &error);
bool load_cached_binding(std::string &error);
bool persist_pairing_snapshot(std::string &error);
bool retry_binding_persistence(std::string &error);
bool binding_persistence_pending();
bool binding_revocation_requested();
bool reset_local_binding(std::string &error);
bool prepare_binding_revocation(std::string &error);
bool complete_binding_revocation(int expected_epoch, int binding_epoch,
                                 bool superseded, std::string &error);
bool complete_pairing_binding(PlatformLinkPairingSnapshot binding, std::string &error);
bool prepare_environment_cache_reset();
PlatformLinkPairingSnapshot pairing_snapshot();
PlatformLinkPairingPresentation pairing_presentation();
bool ui_snapshot_into(PlatformLinkUiSnapshot &);
// Allocation-free scheduler query; full snapshots remain available to readers
// that need account/resource metadata from the same revision.
PlatformLinkBindingState pairing_state();
float pairing_remaining_fraction(uint32_t current_ms);
PlatformLinkBindingStatus binding_status();
bool local_authority_ready();
bool local_management_access_required();
std::string binding_profile_tag();
std::string account_identity();
PlatformLinkAccountScope account_scope();
void update_platform_connection(PlatformLinkPlatformConnectionState state,
                                int status_code = -1,
                                const std::string &error = {});
PlatformLinkPlatformConnectionSnapshot platform_connection_snapshot();
PlatformLinkPlatformConnectionSnapshot platform_connection_last_observation();
void update_platform_request_diagnostics(const std::string &operation,const std::string &error_code);
PlatformLinkPlatformConnectionState platform_connection_state();

PlatformLinkBindingState state_from_server(const std::string &status);
bool account_identity_ready();
bool network_connected();
void add_capabilities(JsonArray capabilities);
void add_runtime_state(JsonObject state);

} // namespace lunaredu::lunaros::platform_link
