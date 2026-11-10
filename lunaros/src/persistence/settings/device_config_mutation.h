#pragma once

#include <string>

#include <ArduinoJson.h>
#include "kernel/static_mutex.h"

#include "lunaredu_lunaros/kernel/text.h"
#include "lunaredu_lunaros/persistence/settings.h"

namespace lunaredu::lunaros {

// Writers hold this across snapshot/Flash/publication. Readers use a separate
// short state mutex and never wait for Flash or another service executor.
StaticMutex &device_config_write_mutex();
void publish_device_config(DeviceConfig &&config);
bool commit_device_config(DeviceConfig config);

bool build_device_wifi_config(const DeviceWifiCredential *credentials,
                              size_t credential_count,
                              uint8_t last_success_index,
                              DeviceWifiConfig &target);

size_t read_device_credentials_array(JsonArrayConst array,
                                     DeviceWifiCredential *credentials);
void initialize_device_config();
bool load_device_config();
std::string derive_local_device_uuid();
std::string build_alpha_suffix_from_uuid(const std::string &uuid);
std::string default_device_base_name();
std::string default_provision_ap_base_name();
std::string build_default_device_name(const std::string &device_uuid);

} // namespace lunaredu::lunaros
