#pragma once
#include <esp_err.h>
#include <cstdint>
#include <string>

namespace lunaredu::device::sdcard {
struct NativeConfig {
  int clk = -1, cmd = -1, d0 = -1;
  std::string mount_path = "/sdcard-native";
  uint32_t max_frequency_khz = 20000;
  int max_files = 5;
  // Physical external pull-ups remain a board requirement.
  bool internal_pullups = false;
};
// Must reserve the SDMMC host, pins and mount namespace exclusively, and
// serialize global FatFS drive registration with other storage owners. Successful
// acquire transfers a lease, failed acquire must leave no lease. Context lives
// until stop succeeds. Synchronous callbacks cannot reenter any NativeSession.
struct NativeResources {
  void *context = nullptr;
  esp_err_t (*acquire)(void *, const NativeConfig &) = nullptr;
  esp_err_t (*release)(void *) = nullptr;
};
struct NativeStatus {
  bool mounted = false;
  bool resources_held = false;
  bool cleanup_pending = false;
  uint64_t capacity_bytes = 0;
  esp_err_t last_error = ESP_OK;
};
// Serialized across native sessions. Does not call the legacy BSP mount, infer
// board pins, create a worker, format, or open/write files. Close all files and
// stop successfully before destroying this object or its resource context.
class NativeSession {
 public:
  NativeSession() = default;
  ~NativeSession() = default; // No hidden hardware I/O or lease release.
  NativeSession(const NativeSession &) = delete;
  NativeSession &operator=(const NativeSession &) = delete;
  esp_err_t start(const NativeConfig &, NativeResources);
  esp_err_t stop(); // Failed cleanup retains the lease and can be retried.
  NativeStatus status() const { return status_; } // Owner serializes observations.
 private:
  esp_err_t record(esp_err_t error);
  esp_err_t cleanup_host();
  esp_err_t cleanup();
  NativeConfig config_{};
  NativeResources resources_{};
  NativeStatus status_{};
  void *card_ = nullptr; // Vendor object stays private.
  void *filesystem_ = nullptr;
  uint8_t drive_ = 0xff;
  bool disk_registered_ = false;
  bool vfs_registered_ = false;
  bool fat_registered_ = false;
  bool vfs_cleanup_pending_ = false;
  bool host_cleanup_pending_ = false;
  bool card_deinitialized_ = false;
};
} // namespace lunaredu::device::sdcard
