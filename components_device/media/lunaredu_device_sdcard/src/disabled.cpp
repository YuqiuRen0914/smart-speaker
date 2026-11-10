#include "sdkconfig.h"
#include <string>
#include <utility>
#include "lunaredu_device_sdcard_storage.h"
#include "lunaredu_device_sdcard_tfcard_store.h"

namespace {
[[maybe_unused]] constexpr const char *kDisabledError = "disabled";
[[maybe_unused]] void set_error(std::string &error, const char *message = kDisabledError) {
  error = message == nullptr ? kDisabledError : message;
}
}

#if !CONFIG_LUNAREDU_DEVICE_ENABLE_TFCARD

namespace lunaredu::device {

bool declare_tfcard_store_projection(
    lunaredu::lunaros::DeviceProjectionRegistry &, std::string &error) {
  set_error(error);
  return false;
}
bool setup_tfcard_store(bool) { return false; }
void tfcard_store_deinit() {}
bool tfcard_store_setup_done() { return false; }
bool tfcard_store_mount() { return false; }
bool tfcard_store_unmount() { return false; }
bool tfcard_store_ready() { return false; }
TfCardStoreStatus tfcard_store_status() {
  TfCardStoreStatus status;
  status.mount_path = "/sdcard";
  status.last_error = kDisabledError;
  return status;
}
bool tfcard_resolve_path(const char *, std::string &) { return false; }

} // namespace lunaredu::device

#endif


#if !CONFIG_LUNAREDU_DEVICE_STORAGE_SDCARD

namespace lunaredu::device {

DeviceSdcardStorageConfig device_sdcard_storage_config_from_kconfig() {
  return {};
}

void device_sdcard_storage_log_config(
    const DeviceSdcardStorageConfig &) {}

esp_err_t device_sdcard_storage_mount(
    const DeviceSdcardStorageConfig &) {
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t device_sdcard_storage_unmount() { return ESP_ERR_NOT_SUPPORTED; }

DeviceSdcardStorageStatus device_sdcard_storage_status() {
  DeviceSdcardStorageStatus status;
  status.last_error = kDisabledError;
  return status;
}

bool device_sdcard_storage_mounted() { return false; }

} // namespace lunaredu::device

#endif
