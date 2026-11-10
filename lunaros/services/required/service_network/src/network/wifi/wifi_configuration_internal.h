#pragma once
#include <string>
#include "configuration/device_config.h"
namespace lunaredu::lunaros {
void poll_wifi_configuration_on_owner();
void reset_wifi_configuration_on_owner();
bool wifi_configuration_busy();
// Serialized owner mutations share reservation, validation and admission.
bool apply_network_saved_wifi_on_owner(uint8_t index, std::string &error);
bool set_network_wifi_config_on_owner(const DeviceWifiCredential *credentials,
    size_t count, const char *device_name, const char *ap_name,
    bool apply, uint8_t index, std::string &error);
}
