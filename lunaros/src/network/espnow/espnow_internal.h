#pragma once

#include "lunaredu_lunaros/network/espnow.h"

namespace lunaredu::lunaros::internal {
bool start_espnow_on_owner(const EspNowPolicy &policy, std::string &error);
void stop_espnow_on_owner();
void poll_espnow_on_owner();
} // namespace lunaredu::lunaros::internal
