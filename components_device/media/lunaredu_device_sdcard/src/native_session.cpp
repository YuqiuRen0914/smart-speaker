#include "lunaredu_device_sdcard_native.h"
#include "sdkconfig.h"
#if CONFIG_LUNAREDU_DEVICE_SDCARD_NATIVE_SESSION
#include "soc/soc_caps.h"
#endif
#if CONFIG_LUNAREDU_DEVICE_SDCARD_NATIVE_SESSION && SOC_SDMMC_HOST_SUPPORTED
#include "lunaredu_device_sdcard_storage.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_vfs.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include <limits>
#include <mutex>

namespace lunaredu::device::sdcard {
namespace {
std::mutex native_mutex;
NativeSession *native_owner = nullptr;
// IDF6.1 keeps a card-identity pointer after disk unregister. Give each drive a
// stable cell instead of leaving a dangling heap pointer or allocating during
// cleanup to clear that SDK mapping. No slot aliases another drive or legacy
// heap card. The pool exists only in this explicitly enabled implementation.
sdmmc_card_t native_cards[FF_VOLUMES]{};
bool valid(const NativeConfig &c) {
  if (!GPIO_IS_VALID_OUTPUT_GPIO(c.clk) || !GPIO_IS_VALID_OUTPUT_GPIO(c.cmd) ||
      !GPIO_IS_VALID_OUTPUT_GPIO(c.d0) || c.clk == c.cmd || c.clk == c.d0 ||
      c.cmd == c.d0 || c.max_frequency_khz < 100 || c.max_frequency_khz > 40000 ||
      c.max_files < 1 || c.max_files > 32 || c.mount_path.size() < 2 ||
      c.mount_path.size() > 31 || c.mount_path.front() != '/') return false;
  for (size_t i = 1; i < c.mount_path.size(); ++i) {
    const char ch = c.mount_path[i];
    if (!(ch >= 'a' && ch <= 'z') && !(ch >= 'A' && ch <= 'Z') &&
        !(ch >= '0' && ch <= '9') && ch != '_' && ch != '-') return false;
  }
  return true;
}
}
esp_err_t NativeSession::record(esp_err_t error) {
  status_.last_error = error;
  status_.cleanup_pending = status_.resources_held &&
      (status_.cleanup_pending || !status_.mounted);
  return error;
}
esp_err_t NativeSession::start(const NativeConfig &config, NativeResources resources) {
  std::lock_guard<std::mutex> lock(native_mutex);
  if (status_.resources_held || native_owner || device_sdcard_storage_mounted())
    return record(ESP_ERR_INVALID_STATE);
  if (!valid(config) || !resources.acquire || !resources.release)
    return record(ESP_ERR_INVALID_ARG);
  sdmmc_host_state_t state{};
  auto result = sdmmc_host_get_state(&state);
  if (result != ESP_OK) return record(result);
  if (state.host_initialized) return record(ESP_ERR_INVALID_STATE);
  config_ = config; resources_ = resources;
  result = resources_.acquire(resources_.context, config_);
  if (result != ESP_OK) { resources_ = {}; return record(result); }
  status_ = {}; status_.resources_held = true; native_owner = this;
  // Recheck after the caller acquired the exclusive host lease. Never deinit a
  // host that was already active before this session's own initialization.
  result = sdmmc_host_get_state(&state);
  if (result != ESP_OK || state.host_initialized) {
    const auto original = result != ESP_OK ? result : ESP_ERR_INVALID_STATE;
    (void)cleanup();
    return record(original);
  }
  result = ff_diskio_get_drive(&drive_);
  if (result != ESP_OK) { drive_ = 0xff; (void)cleanup(); return record(result); }
  if (drive_ > 9 || drive_ >= FF_VOLUMES) {
    drive_ = 0xff; (void)cleanup(); return record(ESP_ERR_INVALID_RESPONSE);
  }
  auto *card = &native_cards[drive_];
  *card = {};
  card_ = card;
  card_deinitialized_ = false;
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = (host.flags & SDMMC_HOST_FLAG_DEINIT_ARG) | SDMMC_HOST_FLAG_1BIT;
  host.max_freq_khz = config_.max_frequency_khz;
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.clk = static_cast<gpio_num_t>(config_.clk);
  slot.cmd = static_cast<gpio_num_t>(config_.cmd);
  slot.d0 = static_cast<gpio_num_t>(config_.d0);
  slot.d1 = slot.d2 = slot.d3 = slot.d4 = slot.d5 = slot.d6 = slot.d7 = GPIO_NUM_NC;
  slot.width = 1; slot.cd = GPIO_NUM_NC; slot.wp = GPIO_NUM_NC;
  slot.flags = config_.internal_pullups ? SDMMC_SLOT_FLAG_INTERNAL_PULLUP : 0;
  host_cleanup_pending_ = true;
  result = host.init();
  if (result == ESP_OK) result = sdmmc_host_init_slot(host.slot, &slot);
  if (result == ESP_OK) result = sdmmc_card_init(&host, card);
  if (result == ESP_OK && (card->csd.capacity <= 0 || card->csd.sector_size <= 0 ||
      uint64_t(card->csd.capacity) > std::numeric_limits<uint64_t>::max() /
          uint64_t(card->csd.sector_size))) result = ESP_ERR_INVALID_RESPONSE;
  if (result != ESP_OK) { (void)cleanup(); return record(result); }
  ff_diskio_register_sdmmc(drive_, card); disk_registered_ = true;
  const char fat_drive[]{char('0' + drive_), ':', 0};
  const esp_vfs_fat_conf_t mount{config_.mount_path.c_str(), fat_drive,
                              static_cast<size_t>(config_.max_files)};
  FATFS *fs = nullptr;
  result = esp_vfs_fat_register(&mount, &fs);
  if (result != ESP_OK) {
    // INVALID_STATE may return somebody else's existing FATFS; never unmount it.
    (void)cleanup(); return record(result);
  }
  vfs_registered_ = true; filesystem_ = fs;
  if (!fs) { (void)cleanup(); return record(ESP_ERR_INVALID_RESPONSE); }
  // f_mount installs its pointer before probing media, even on a failed probe.
  fat_registered_ = true;
  if (f_mount(fs, fat_drive, 1) != FR_OK) {
    (void)cleanup(); return record(ESP_FAIL); // Never format an unknown filesystem.
  }
  status_.mounted = true;
  status_.capacity_bytes = uint64_t(card->csd.capacity) * uint64_t(card->csd.sector_size);
  return record(ESP_OK);
}
esp_err_t NativeSession::cleanup_host() {
  if (!host_cleanup_pending_) return ESP_OK;
  sdmmc_host_state_t state{};
  auto result = sdmmc_host_get_state(&state);
  if (result != ESP_OK) return result;
  if (state.host_initialized) {
    result = sdmmc_host_deinit();
    if (result != ESP_OK) return result;
    result = sdmmc_host_get_state(&state);
    if (result != ESP_OK) return result;
    if (state.host_initialized) return ESP_ERR_INVALID_STATE;
  }
  host_cleanup_pending_ = false;
  return ESP_OK;
}
esp_err_t NativeSession::cleanup() {
  if (fat_registered_) {
    const char drive[]{char('0' + drive_), ':', 0};
    if (f_mount(nullptr, drive, 0) != FR_OK) return record(ESP_FAIL);
    fat_registered_ = false;
  }
  status_.mounted = false; status_.capacity_bytes = 0;
  if (vfs_registered_) {
    // IDF6.1 consumes the FAT context even if its VFS unregister fails. Retry
    // only the remaining VFS registration; never pass a freed FATFS again.
    vfs_registered_ = false; filesystem_ = nullptr;
    const auto result = esp_vfs_fat_unregister_path(config_.mount_path.c_str());
    if (result != ESP_OK) { vfs_cleanup_pending_ = true; return record(result); }
  }
  if (vfs_cleanup_pending_) {
    const auto result = esp_vfs_unregister(config_.mount_path.c_str());
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return record(result);
    vfs_cleanup_pending_ = false;
  }
  if (disk_registered_) {
    ff_diskio_unregister(drive_); disk_registered_ = false;
  }
  drive_ = 0xff;
  if (card_ && !card_deinitialized_) {
    auto *card = static_cast<sdmmc_card_t *>(card_);
    const auto result = sdmmc_card_deinit(card);
    if (result != ESP_OK) return record(result);
    card_deinitialized_ = true;
  }
  const auto closed = cleanup_host();
  if (closed != ESP_OK) return record(closed);
  if (card_) {
    *static_cast<sdmmc_card_t *>(card_) = {};
    card_ = nullptr; card_deinitialized_ = false;
  }
  const auto released = resources_.release(resources_.context);
  if (released != ESP_OK) return record(released);
  status_ = {}; resources_ = {}; config_ = {}; native_owner = nullptr;
  return ESP_OK;
}
esp_err_t NativeSession::stop() {
  std::lock_guard<std::mutex> lock(native_mutex);
  if (!status_.resources_held) return record(ESP_OK);
  if (native_owner != this) return record(ESP_ERR_INVALID_STATE);
  status_.cleanup_pending = true;
  return record(cleanup());
}
} // namespace lunaredu::device::sdcard
#else
namespace lunaredu::device::sdcard {
esp_err_t NativeSession::record(esp_err_t error) { status_.last_error = error; return error; }
esp_err_t NativeSession::start(const NativeConfig &, NativeResources) {
  return record(ESP_ERR_NOT_SUPPORTED);
}
esp_err_t NativeSession::stop() { return record(ESP_OK); }
} // namespace lunaredu::device::sdcard
#endif
