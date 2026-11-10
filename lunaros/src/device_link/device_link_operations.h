#pragma once

#include <cstdint>
#include <string>

namespace lunaredu::lunaros::device_link {

inline constexpr uint32_t kMaximumDiscoveryDurationMs = 5 * 60 * 1000;

bool publish_presence();
bool refresh_and_apply_firmware_update();

std::string create_pairing_nonce();
bool enroll_open_claim();
bool create_pairing_session();
bool poll_pairing_session();
bool cancel_pairing_session();
bool unbind_device();

bool begin_discovery(uint32_t duration_ms);
bool publish_discovery_announcement();
bool discovery_active();

bool poll_commands();

} // namespace lunaredu::lunaros::device_link
