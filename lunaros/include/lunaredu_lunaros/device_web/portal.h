#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace lunaredu::lunaros {
// Immutable image bytes or an explicitly named filesystem page. Embedded bytes
// win when both are supplied. Buffers must outlive the HTTP service.
struct DeviceWebPage {
  const uint8_t *data = nullptr;
  size_t size = 0;
  const char *storage_path = nullptr;
};
struct DeviceWebConfig {
  const char *product_name = nullptr;
  DeviceWebPage home{};
  DeviceWebPage network{};
  DeviceWebPage binding{};
  DeviceWebPage maintenance{};
};
// Call once before setup_device_api. With no call, all routes use the built-in
// portal. Missing overrides fall back page-by-page; /recovery always bypasses
// overrides. Invalid configuration leaves the previous configuration intact.
bool configure_device_web(const DeviceWebConfig &config, std::string &error);
} // namespace lunaredu::lunaros
