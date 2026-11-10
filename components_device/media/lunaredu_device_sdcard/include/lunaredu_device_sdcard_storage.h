#pragma once

#include <string>

#include <esp_err.h>

namespace lunaredu::device {

struct DeviceSdcardStorageConfig {
  bool enabled = false;
  const char *mount_path = "/sdcard";
};

struct DeviceSdcardStorageStatus {
  bool mounted = false;
  std::string mount_path = "/sdcard";
  uint64_t capacity_bytes = 0;
  std::string last_error;
};

DeviceSdcardStorageConfig device_sdcard_storage_config_from_kconfig();
void device_sdcard_storage_log_config(
    const DeviceSdcardStorageConfig &config);
esp_err_t device_sdcard_storage_mount(
    const DeviceSdcardStorageConfig &config);
esp_err_t device_sdcard_storage_unmount();
DeviceSdcardStorageStatus device_sdcard_storage_status();
bool device_sdcard_storage_mounted();

} // namespace lunaredu::device
