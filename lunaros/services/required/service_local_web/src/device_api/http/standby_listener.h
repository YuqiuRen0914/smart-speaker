#pragma once
#include <cstdint>
#include <string>
namespace lunaredu::lunaros::web_internal {
bool standby_listener_start(std::string &error);
void standby_listener_stop();
// Bounded nonblocking poll, true after sending a navigation wake response.
bool standby_listener_poll(uint32_t now_ms);
}
