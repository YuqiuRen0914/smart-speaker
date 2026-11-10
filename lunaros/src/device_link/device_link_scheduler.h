#pragma once

#include <cstdint>
#include "lunaredu_lunaros/device_link/device_link.h"

namespace lunaredu::lunaros::device_link {

void tick_scheduler();
void reset_scheduler();
void request_pairing();
void request_pairing_cancel();
bool request_unbind();
DeviceLinkUnbindState unbind_state();
void request_discovery(uint32_t duration_ms);
void request_firmware_update();
bool request_environment_switch(bool production);
bool environment_restart_pending();
bool request_environment_restart();

} // namespace lunaredu::lunaros::device_link
