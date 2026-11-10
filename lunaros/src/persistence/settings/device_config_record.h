#pragma once

#include "lunaredu_lunaros/persistence/settings.h"

namespace lunaredu::lunaros {

// One NVS blob is the publication unit. Length-prefixed strings preserve UTF-8
// without JSON escape expansion; the bound covers all five maximum credentials.
inline constexpr size_t kDeviceConfigRecordMaxBytes = 2048;
inline constexpr const char *kDeviceConfigRecordKey = "snapshot";
bool encode_device_config_record(const DeviceConfig &config, std::string &record);
bool decode_device_config_record(const std::string &record, DeviceConfig &config);

} // namespace lunaredu::lunaros
