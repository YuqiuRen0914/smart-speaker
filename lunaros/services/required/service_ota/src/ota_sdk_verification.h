#pragma once
#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <atomic>
#include <mutex>

namespace lunaredu::lunaros {
// IDF's app-side bootloader_mmap has one process-wide mapping handle. All
// explicit and SDK-internal image scans must share this lock across owners.
inline std::mutex &ota_sdk_verification_mutex() {
  static std::mutex mutex;
  return mutex;
}
inline esp_err_t ota_sdk_verify_image(esp_image_load_mode_t mode,
    const esp_partition_pos_t *partition, esp_image_metadata_t *image) {
  std::lock_guard<std::mutex> lock(ota_sdk_verification_mutex());
  return esp_image_verify(mode, partition, image);
}
inline esp_err_t ota_sdk_end(esp_ota_handle_t handle) {
  std::lock_guard<std::mutex> lock(ota_sdk_verification_mutex());
  return esp_ota_end(handle);
}
inline esp_err_t ota_sdk_set_boot_partition(const esp_partition_t *partition) {
  std::lock_guard<std::mutex> lock(ota_sdk_verification_mutex());
  return esp_ota_set_boot_partition(partition);
}
inline esp_err_t ota_sdk_rollback_and_reboot() {
  std::lock_guard<std::mutex> lock(ota_sdk_verification_mutex());
  return esp_ota_mark_app_invalid_rollback_and_reboot();
}
inline esp_err_t ota_sdk_partition_sha256(const esp_partition_t *partition, uint8_t *digest) {
  std::lock_guard<std::mutex> lock(ota_sdk_verification_mutex());
  return esp_partition_get_sha256(partition, digest);
}
// Status caches must also expire after a partial rewrite retaining the old
// descriptor. The writer owns this in-memory generation, not a boot selector.
inline std::atomic<uint32_t> &ota_sdk_image_generation() {
  static std::atomic<uint32_t> generation{0};
  return generation;
}
inline std::atomic<bool> &ota_sdk_image_write_active() {
  static std::atomic<bool> active{false};
  return active;
}
inline void ota_sdk_image_write_begin() {
  ota_sdk_image_write_active().store(true);
  ++ota_sdk_image_generation();
}
inline void ota_sdk_image_write_end() {
  if (ota_sdk_image_write_active().exchange(false)) ++ota_sdk_image_generation();
}
} // namespace lunaredu::lunaros
