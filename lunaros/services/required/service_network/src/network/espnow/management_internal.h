#pragma once
#include "lunaros_network_espnow_management.h"
namespace lunaredu::lunaros::internal {
bool stop_espnow_management(std::string &error);
void poll_espnow_management();
bool bind_espnow_provisioning();
bool espnow_responder_busy();
bool espnow_client_busy();
bool stop_espnow_client(std::string &error);
void poll_espnow_client();
}
