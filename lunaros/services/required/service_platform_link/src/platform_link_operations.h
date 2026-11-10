#pragma once

#include <cstdint>
#include <string>
#include <ArduinoJson.h>
#include "lunaros_platform_link.h"

namespace lunaredu::lunaros::platform_link {

inline constexpr uint32_t kMaximumDiscoveryDurationMs = 5 * 60 * 1000;

bool publish_presence(int *http_status = nullptr);
bool refresh_and_apply_firmware_update();
bool refresh_available_firmware(bool report_failure = false);
bool request_firmware_check();
PlatformLinkFirmwareCheckState firmware_check_state();

std::string create_pairing_nonce();
bool enroll_open_claim(int *http_status = nullptr);
// Network owner only. Profile records are synchronization hints, never binding
// authority. A changed profile defers this presence's stale resource catalog.
void record_product_profile_success();
bool reconcile_product_profile_after_presence();
// Read-only readiness for ticket-refresh entry points; never enrolls or writes.
bool product_profile_current();
bool create_pairing_session();
bool poll_pairing_session();
bool cancel_pairing_session();
bool unbind_device();

bool begin_discovery(uint32_t duration_ms);
bool publish_discovery_announcement();
bool discovery_active();
bool discovery_announcement_needed();
void invalidate_discovery_announcement();
void end_discovery();

bool poll_commands();
bool execute_realtime_teleop(JsonObjectConst frame, JsonDocument &reply);
void tick_realtime_control_channel();
void stop_realtime_control_channel();

} // namespace lunaredu::lunaros::platform_link
