#pragma once
#include "configuration/device_config.h"
#include <string_view>
namespace lunaredu::lunaros {
// New radio/persistence requests share exact byte-string admission. Historical
// records remain readable under their versioned codec; reading is not admission.
bool valid_device_wifi_credentials(std::string_view ssid, std::string_view password);
enum class DeviceWifiListValidity { Valid, Invalid, Duplicate };
DeviceWifiListValidity validate_device_wifi_list(const DeviceWifiCredential *items,size_t count);
bool valid_device_wifi_list(const DeviceWifiCredential *items,size_t count);
}
