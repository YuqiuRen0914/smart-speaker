#include "bsp/boards/general/v2.h"
#include <cstring>

// Keep the candidate revision queryable without constructing any hardware,
// bus handle, mutex or ownership registry in existing product builds.
namespace {
constexpr const char *names[]={"v2.zone_a","v2.zone_b","v2.zone_c","v2.audio",
  "v2.interface_aux","v2.common_i2c","v2.interface_i2c","v2.spi_bus","v2.screen",
  "v2.touch_int","v2.boot","v2.rgb","v2.usb","v2.camera","v2.tfcard"};
static_assert(sizeof(names)/sizeof(names[0])==BSP_V2_RESOURCE_COUNT);
bool resource_valid(bsp_v2_resource_t id) {return id>=0 && id<BSP_V2_RESOURCE_COUNT;}
bool owner_valid(const char *owner) {return owner && *owner && std::strlen(owner)<BSP_V2_OWNER_MAX;}
bool bus_valid(bsp_v2_i2c_t id) {return id==bsp_v2_i2c_t::common || id==bsp_v2_i2c_t::expansion;}
bool transfer_valid(bsp_v2_i2c_t id,uint8_t address,int timeout) {
  return bus_valid(id) && ((address>=0x08 && address<=0x77) || address==0x7c) && timeout>0 && timeout<=1000;
}
}

const bsp_v2_board_t *bsp_v2_board() {return nullptr;}
esp_err_t bsp_v2_camera_config(bsp_v2_camera_t *out) {
  if(!out)return ESP_ERR_INVALID_ARG;
  *out={};return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t bsp_v2_resource_acquire(bsp_v2_resource_t id,const char *owner) {
  return resource_valid(id) && owner_valid(owner)?ESP_ERR_NOT_SUPPORTED:ESP_ERR_INVALID_ARG;
}
esp_err_t bsp_v2_resource_release(bsp_v2_resource_t id,const char *owner) {
  return resource_valid(id) && owner_valid(owner)?ESP_OK:ESP_ERR_INVALID_ARG;
}
bool bsp_v2_resource_is_acquired(bsp_v2_resource_t) {return false;}
bool bsp_v2_resource_contains_pin(bsp_v2_resource_t,int) {return false;}
bool bsp_v2_resource_observe(bsp_v2_resource_t id,bsp_v2_resource_observation_t *out) {
  if(!resource_valid(id) || !out)return false;
  *out={};out->name=names[id];return true;
}
size_t bsp_v2_resource_signals(bsp_v2_resource_t,bsp_v2_signal_t*,size_t) {return 0;}
esp_err_t bsp_v2_i2c_init(bsp_v2_i2c_t id) {return bus_valid(id)?ESP_ERR_NOT_SUPPORTED:ESP_ERR_INVALID_ARG;}
esp_err_t bsp_v2_i2c_deinit(bsp_v2_i2c_t id) {return bus_valid(id)?ESP_ERR_NOT_SUPPORTED:ESP_ERR_INVALID_ARG;}
bool bsp_v2_i2c_ready(bsp_v2_i2c_t) {return false;}
i2c_master_bus_handle_t bsp_v2_i2c_handle(bsp_v2_i2c_t) {return nullptr;}
esp_err_t bsp_v2_i2c_probe(bsp_v2_i2c_t id,uint8_t address,int timeout) {
  return transfer_valid(id,address,timeout)?ESP_ERR_NOT_SUPPORTED:ESP_ERR_INVALID_ARG;
}
esp_err_t bsp_v2_i2c_transfer(bsp_v2_i2c_t id,uint8_t address,const uint8_t *write_data,
    size_t write_size,uint8_t *read_data,size_t read_size,int timeout) {
  if(!transfer_valid(id,address,timeout) || (!write_size && !read_size) ||
      (write_size && !write_data) || (read_size && !read_data))return ESP_ERR_INVALID_ARG;
  return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t bsp_v2_spi_init() {return ESP_ERR_NOT_SUPPORTED;}
esp_err_t bsp_v2_spi_deinit() {return ESP_ERR_NOT_SUPPORTED;}
bool bsp_v2_spi_ready() {return false;}
esp_err_t bsp_v2_audio_claim(const int *pins,size_t count,bsp_v2_audio_claim_t *out) {
  return pins && count && out?ESP_ERR_NOT_SUPPORTED:ESP_ERR_INVALID_ARG;
}
esp_err_t bsp_v2_audio_release(bsp_v2_audio_claim_t *claim) {
  if(!claim)return ESP_ERR_INVALID_ARG;
  claim->lease_id=0;return ESP_OK;
}
esp_err_t bsp_v2_sdcard_mount(const char*) {return ESP_ERR_NOT_SUPPORTED;}
esp_err_t bsp_v2_sdcard_unmount() {return ESP_OK;}
bsp_v2_sdcard_status_t bsp_v2_sdcard_get_status() {
  bsp_v2_sdcard_status_t status{};std::strcpy(status.mount_path,"/sdcard");return status;
}
