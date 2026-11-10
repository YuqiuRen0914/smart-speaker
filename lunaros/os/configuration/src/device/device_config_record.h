#pragma once

#include "configuration/device_config.h"

namespace lunaredu::lunaros {

// One NVS blob is the publication unit. Length-prefixed strings preserve UTF-8
// without JSON escape expansion; the bound covers all five maximum credentials.
inline constexpr size_t kDeviceConfigRecordMaxBytes = 2048;
inline constexpr const char *kDeviceConfigRecordKey = "snapshot";
bool encode_device_config_record(const DeviceConfig &config, std::string &record,
                                 bool legacy_compatible = false);
// LDC8 is the supported bridge format (no development URL). All other retired
// formats and split keys are unsupported. A bridge writer preserves LDC8 when
// no development URL needs LDC9, so old boot firmware remains rollback-capable.
bool decode_device_config_record(const std::string &record, DeviceConfig &config,
                                 bool *migration_required = nullptr);

} // namespace lunaredu::lunaros
