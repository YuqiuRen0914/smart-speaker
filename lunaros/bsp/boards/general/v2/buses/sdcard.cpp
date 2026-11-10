#include "bsp/boards/general/v2.h"
#include <driver/sdspi_host.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <cstring>
#include <cstdio>
#include <mutex>

namespace {
std::mutex mutex;
sdmmc_card_t *card=nullptr;
char path[128]="/sdcard";
bool claimed=false;
const char *last_error="";
constexpr const char *owner="v2.tfcard";
}
esp_err_t bsp_v2_sdcard_mount(const char *mount_path) {
  std::lock_guard<std::mutex> lock(mutex);
  const auto *b=bsp_v2_board();
  if(!b || !b->tf_enabled) return ESP_ERR_NOT_SUPPORTED;
  const char *requested=mount_path?mount_path:"/sdcard";
  if(requested[0]!='/' || std::strlen(requested)>=sizeof(path)) return ESP_ERR_INVALID_ARG;
  if(card) return std::strcmp(path,requested)==0?ESP_OK:ESP_ERR_INVALID_STATE;
  if(claimed) return ESP_ERR_INVALID_STATE;
  auto e=bsp_v2_spi_init();
  if(e!=ESP_OK) { last_error=esp_err_to_name(e); return e; }
  // Do not put a card into SPI mode while display communication is active.
  const bool screen=b->screen.mode!=bsp_v2_display_t::none;
  bool guarded=false;
  if(screen) {
    e=bsp_v2_resource_acquire(BSP_V2_RESOURCE_SCREEN,owner);
    if(e!=ESP_OK) { last_error=esp_err_to_name(e); return e; }
    guarded=true;
  }
  e=bsp_v2_resource_acquire(BSP_V2_RESOURCE_TFCARD,owner);
  if(e==ESP_OK) {
    claimed=true; std::strcpy(path,requested);
    sdmmc_host_t host=SDSPI_HOST_DEFAULT();
    host.slot=static_cast<int>(b->spi.host); host.max_freq_khz=b->tf_clock_hz/1000;
    sdspi_device_config_t device=SDSPI_DEVICE_CONFIG_DEFAULT();
    device.host_id=b->spi.host; device.gpio_cs=static_cast<gpio_num_t>(b->tf_cs);
    device.gpio_cd=GPIO_NUM_NC; device.gpio_wp=GPIO_NUM_NC;
    esp_vfs_fat_mount_config_t mount=VFS_FAT_MOUNT_DEFAULT_CONFIG();
    mount.format_if_mount_failed=false; mount.max_files=8; mount.allocation_unit_size=16384;
    e=esp_vfs_fat_sdspi_mount(path,&host,&device,&mount,&card);
    // Failed initialization leaves an unknown physical card mode; retain the
    // CS claim until explicit cleanup. The shared host is never freed here.
  }
  if(guarded) {
    const auto r=bsp_v2_resource_release(BSP_V2_RESOURCE_SCREEN,owner);
    if(e==ESP_OK) e=r;
  }
  last_error=e==ESP_OK?"":esp_err_to_name(e);
  return e;
}
esp_err_t bsp_v2_sdcard_unmount() {
  std::lock_guard<std::mutex> lock(mutex);
  if(card) {
    const auto e=esp_vfs_fat_sdcard_unmount(path,card);
    if(e!=ESP_OK) { last_error=esp_err_to_name(e); return e; }
    card=nullptr;
  }
  if(claimed) {
    const auto e=bsp_v2_resource_release(BSP_V2_RESOURCE_TFCARD,owner);
    if(e!=ESP_OK) { last_error=esp_err_to_name(e); return e; }
    claimed=false;
  }
  last_error="";
  return ESP_OK;
}
bsp_v2_sdcard_status_t bsp_v2_sdcard_get_status() {
  std::lock_guard<std::mutex> lock(mutex);
  bsp_v2_sdcard_status_t status{};
  status.mounted=card!=nullptr;
  std::strcpy(status.mount_path,path);
  status.capacity_bytes=card?uint64_t(card->csd.capacity)*card->csd.sector_size:0;
  std::snprintf(status.last_error,sizeof(status.last_error),"%s",last_error);
  return status;
}
