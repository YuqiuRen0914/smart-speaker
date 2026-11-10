#pragma once

namespace lunaredu::lunaros {

// `lunaredu_config` 只拥有设备实例默认值：
// 设备展示名、默认配网热点名，以及首次写入 NVS 的默认配置内容。
constexpr const char *DEVICE_CONFIG_DEFAULT_DEVICE_NAME =
    "Lunar Hardware Device";
constexpr const char *DEVICE_CONFIG_DEFAULT_PROVISION_AP_NAME =
    "LunarHardware";

} // namespace lunaredu::lunaros
