#include "lunaredu_device_sdcard_storage.h"
#include "lunaredu_device_sdcard_tfcard_store.h"

#include <esp_log.h>

#include "bsp/lunaredu_bsp.h"
#include "sdkconfig.h"

namespace lunaredu::device {
namespace {

constexpr const char *kLogTag = "device_storage";

DeviceSdcardStorageStatus convert_status(const bsp_sdcard_status_t &status) {
  return {
      .mounted = status.mounted,
      .mount_path = status.mount_path == nullptr ? "/sdcard" : status.mount_path,
      .capacity_bytes = status.capacity_bytes,
      .last_error = status.last_error == nullptr ? "" : status.last_error,
  };
}

} // namespace

DeviceSdcardStorageConfig device_sdcard_storage_config_from_kconfig() {
  DeviceSdcardStorageConfig config;
#if CONFIG_LUNAREDU_DEVICE_STORAGE_SDCARD
  config.enabled = true;
  config.mount_path = CONFIG_LUNAREDU_DEVICE_STORAGE_SDCARD_MOUNT_PATH;
#else
  config.enabled = false;
#endif
  return config;
}

void device_sdcard_storage_log_config(
    const DeviceSdcardStorageConfig &config) {
  ESP_LOGI(kLogTag, "sdcard enabled=%d mount_path=%s", config.enabled,
           config.mount_path == nullptr ? "(default)" : config.mount_path);
}

esp_err_t device_sdcard_storage_mount(
    const DeviceSdcardStorageConfig &config) {
  device_sdcard_storage_log_config(config);
  if (!config.enabled) {
    return ESP_ERR_NOT_SUPPORTED;
  }

  const esp_err_t result = bsp_sdcard_mount(config.mount_path);
  if (result != ESP_OK) {
    const bsp_sdcard_status_t status = bsp_sdcard_get_status();
    ESP_LOGW(kLogTag, "SD card mount failed: %s last_error=%s",
             esp_err_to_name(result),
             status.last_error == nullptr ? "(none)" : status.last_error);
    return result;
  }

  const bsp_sdcard_status_t status = bsp_sdcard_get_status();
  ESP_LOGI(kLogTag, "SD card mounted at %s capacity=%llu bytes",
           status.mount_path == nullptr ? "(unknown)" : status.mount_path,
           static_cast<unsigned long long>(status.capacity_bytes));
  return ESP_OK;
}

esp_err_t device_sdcard_storage_unmount() { return bsp_sdcard_unmount(); }

DeviceSdcardStorageStatus device_sdcard_storage_status() {
  return convert_status(bsp_sdcard_get_status());
}

bool device_sdcard_storage_mounted() {
  return bsp_sdcard_get_status().mounted;
}

} // namespace lunaredu::device
