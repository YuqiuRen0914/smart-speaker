#pragma once

namespace lunaredu::lunaros {

// Stable identifiers for the shared LunarOS services. Products may add their
// own services, but must not create aliases for these common lifecycle nodes.
inline constexpr const char *kSystemRuntimeServiceId = "system.runtime";
inline constexpr const char *kSettingsServiceId = "persistence.settings";
inline constexpr const char *kStorageServiceId = "storage.filesystem";
inline constexpr const char *kUpdateServiceId = "update.ota";
inline constexpr const char *kNetworkRuntimeServiceId = "network.runtime";
inline constexpr const char *kDeviceApiServiceId = "device.api";
inline constexpr const char *kResourceServiceId = "resource.delivery";
inline constexpr const char *kDeviceLinkServiceId = "device.link";

} // namespace lunaredu::lunaros
