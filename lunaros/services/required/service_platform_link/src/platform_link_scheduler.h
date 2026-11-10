#pragma once

#include <cstdint>
#include "lunaros_platform_link.h"

namespace lunaredu::lunaros::platform_link {

void tick_scheduler();
struct ConnectionRetrySnapshot {
  bool queued = false, running = false;
  uint32_t wait_ms = 0;
};
ConnectionRetrySnapshot connection_retry_snapshot();
// Pending OTA may need authenticated binding recovery to pass management
// health. No pairing, business commands, assets or firmware offers run here.
void tick_boot_authority_recovery();
// Only a newly persisted identity skips the initial authenticated presence.
void request_open_claim_enrollment();
bool reset_scheduler();
void request_pairing();
bool pairing_request_pending();
bool pairing_cancel_pending();
void request_pairing_cancel();
bool request_unbind();
PlatformLinkUnbindState unbind_state();
void request_discovery(uint32_t duration_ms);
void request_firmware_update();
bool firmware_update_pending();
bool request_environment_switch(bool production, const std::string &origin);
bool environment_switch_pending();
bool environment_restart_pending();
bool request_environment_restart();

} // namespace lunaredu::lunaros::platform_link
