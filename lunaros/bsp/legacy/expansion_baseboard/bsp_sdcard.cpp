#include "legacy/expansion_baseboard/pins.h"
#include "bsp/lunaredu_bsp.h"

#include <driver/sdmmc_host.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <cstring>
#include <mutex>

namespace {

constexpr const char *kDefaultMountPath = "/sdcard";

sdmmc_card_t *g_card = nullptr;
char g_mount_path[128] = "/sdcard";
bool g_claimed = false;
std::mutex g_sdcard_mutex;
uint64_t g_capacity_bytes = 0;
const char *g_last_error = "";

} // 鍖垮悕鍛藉悕绌洪棿

esp_err_t bsp_sdcard_mount(const char *mount_path) {
  std::lock_guard<std::mutex> lock(g_sdcard_mutex);
  const char *path = mount_path == nullptr ? kDefaultMountPath : mount_path;
  if (path[0] != '/' || std::strlen(path) >= sizeof(g_mount_path)) return ESP_ERR_INVALID_ARG;
  if (g_card != nullptr) {
    return std::strcmp(path, g_mount_path) == 0 ? ESP_OK : ESP_ERR_INVALID_STATE;
  }
  if (g_claimed) return ESP_ERR_INVALID_STATE;

  const esp_err_t claim = bsp_resource_acquire(BSP_RESOURCE_TFCARD, "tfcard");
  if (claim != ESP_OK) {
    g_last_error = esp_err_to_name(claim);
    return claim;
  }

  std::strcpy(g_mount_path, path);
  g_claimed = true;

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;

  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.clk =
      static_cast<gpio_num_t>(lunaredu::bsp::pins::kTfCardClock);
  slot_config.cmd =
      static_cast<gpio_num_t>(lunaredu::bsp::pins::kTfCardCommand);
  slot_config.d0 =
      static_cast<gpio_num_t>(lunaredu::bsp::pins::kTfCardData0);
  slot_config.d1 = GPIO_NUM_NC;
  slot_config.d2 = GPIO_NUM_NC;
  slot_config.d3 = GPIO_NUM_NC;
  slot_config.d4 = GPIO_NUM_NC;
  slot_config.d5 = GPIO_NUM_NC;
  slot_config.d6 = GPIO_NUM_NC;
  slot_config.d7 = GPIO_NUM_NC;
  slot_config.width = lunaredu::bsp::pins::kTfCardBusWidth;
  slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  esp_vfs_fat_sdmmc_mount_config_t mount_config =
      VFS_FAT_MOUNT_DEFAULT_CONFIG();
  mount_config.format_if_mount_failed = false;
  mount_config.max_files = 8;
  mount_config.allocation_unit_size = 16 * 1024;

  const esp_err_t result = esp_vfs_fat_sdmmc_mount(
      g_mount_path, &host, &slot_config, &mount_config, &g_card);
  if (result != ESP_OK) {
    g_card = nullptr;
    g_capacity_bytes = 0;
    g_last_error = esp_err_to_name(result);
    if (bsp_resource_release(BSP_RESOURCE_TFCARD, "tfcard") == ESP_OK) g_claimed = false;
    return result;
  }

  g_capacity_bytes = static_cast<uint64_t>(g_card->csd.capacity) *
                     static_cast<uint64_t>(g_card->csd.sector_size);
  g_last_error = "";
  return ESP_OK;
}

esp_err_t bsp_sdcard_unmount() {
  std::lock_guard<std::mutex> lock(g_sdcard_mutex);
  if (g_card != nullptr) {
    const esp_err_t result = esp_vfs_fat_sdcard_unmount(g_mount_path, g_card);
    if (result != ESP_OK) {
      g_last_error = esp_err_to_name(result);
      return result;
    }
    g_card = nullptr;
    g_capacity_bytes = 0;
  }
  if (g_claimed) {
    const esp_err_t result = bsp_resource_release(BSP_RESOURCE_TFCARD, "tfcard");
    if (result != ESP_OK) { g_last_error = esp_err_to_name(result); return result; }
    g_claimed = false;
  }
  g_last_error = "";
  return ESP_OK;
}

bsp_sdcard_status_t bsp_sdcard_get_status() {
  std::lock_guard<std::mutex> lock(g_sdcard_mutex);
  return {
      .mounted = g_card != nullptr,
      .mount_path = g_mount_path,
      .capacity_bytes = g_capacity_bytes,
      .last_error = g_last_error,
  };
}
