#pragma once

#include "lunaros_network_espnow.h"

namespace lunaredu::lunaros::internal {
bool start_espnow_on_owner(const EspNowPolicy &policy, std::string &error);
// True only after SDK resources and captured product deliveries have retired.
// False retains ownership, denies restart and preserves a retryable reason.
bool stop_espnow_on_owner(std::string &error);
void poll_espnow_on_owner();
} // namespace lunaredu::lunaros::internal
