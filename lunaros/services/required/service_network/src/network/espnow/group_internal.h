#pragma once
#include <string>
namespace lunaredu::lunaros::internal {
bool espnow_group_busy();
bool stop_espnow_group(std::string &);
void poll_espnow_group();
void drain_espnow_group();
bool register_espnow_group_broadcast(std::string &);
}
