#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace lunaredu::lunaros::internal {
bool handle_espnow_ota_transfer(const uint8_t *,size_t,uint8_t *,size_t &,std::string &);
void poll_espnow_ota_transfer();
void poll_espnow_ota_sender();
bool stop_espnow_ota_transfer(std::string &error);
bool stop_espnow_ota_sender(std::string &error);
}
