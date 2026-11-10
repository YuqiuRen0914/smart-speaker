#pragma once

namespace lunaredu::lunaros {

// `lunaredu_config` 只拥有设备实例默认值：
// 设备展示名、默认配网热点名，以及首次写入 NVS 的默认配置内容。
constexpr const char *DEVICE_CONFIG_DEFAULT_DEVICE_NAME =
    "Lunar Hardware Device";
constexpr const char *DEVICE_CONFIG_DEFAULT_PROVISION_AP_NAME =
    "LunarHardware";

// Migration-only values. Firmware no longer ships or injects Wi-Fi
// credentials; these constants only identify credentials written by older
// schemas so an upgrade can remove them from persistent storage.
constexpr const char *DEVICE_CONFIG_RETIRED_DEFAULT_WIFI_SSID =
    "lunaredu-student";
constexpr const char *DEVICE_CONFIG_RETIRED_DEFAULT_WIFI_PASSWORD =
    "lunaredu";
constexpr const char *DEVICE_CONFIG_LEGACY_DEFAULT_WIFI_SSID = "lunaredu";
constexpr const char *DEVICE_CONFIG_LEGACY_DEFAULT_WIFI_PASSWORD = "lunaredu";

} // namespace lunaredu::lunaros
