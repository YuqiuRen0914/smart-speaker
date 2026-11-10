#include "boards/general/v2/resources/descriptor.h"
#include <cstring>
#include <mutex>

namespace lunaredu::bsp_v2 {
const ResourceTable &resources() {
  static const ResourceTable table=[] {
    ResourceTable t{};
    constexpr const char *names[]={"v2.zone_a","v2.zone_b","v2.zone_c","v2.audio",
      "v2.interface_aux","v2.common_i2c","v2.interface_i2c","v2.spi_bus","v2.screen",
      "v2.touch_int","v2.boot","v2.rgb","v2.usb","v2.camera","v2.tfcard"};
    static_assert(sizeof(names)/sizeof(names[0])==BSP_V2_RESOURCE_COUNT);
    for(int i=0;i<BSP_V2_RESOURCE_COUNT;++i)t[i]={static_cast<bsp_v2_resource_t>(i),names[i],0,false};
    if(bsp_v2_board())fill_resources(t);
    return t;
  }();
  return table;
}
}
namespace {
std::mutex mutex;
char owners[BSP_V2_RESOURCE_COUNT][BSP_V2_OWNER_MAX]{};
bool valid(bsp_v2_resource_t id) {return id>=0 && id<BSP_V2_RESOURCE_COUNT;}
bool owner_valid(const char *s) {return s && *s && std::strlen(s)<BSP_V2_OWNER_MAX;}
}
esp_err_t bsp_v2_resource_acquire(bsp_v2_resource_t id,const char *owner) {
  if(!valid(id) || !owner_valid(owner))return ESP_ERR_INVALID_ARG;
  std::lock_guard<std::mutex> lock(mutex);
  const auto &table=lunaredu::bsp_v2::resources();const auto &d=table[id];
  if(!d.available)return ESP_ERR_NOT_SUPPORTED;
  if(owners[id][0])return std::strcmp(owners[id],owner)==0?ESP_OK:ESP_ERR_INVALID_STATE;
  for(const auto &other:table)if(owners[other.id][0] && (d.pin_mask & other.pin_mask))return ESP_ERR_INVALID_STATE;
  std::strcpy(owners[id],owner);return ESP_OK;
}
esp_err_t bsp_v2_resource_release(bsp_v2_resource_t id,const char *owner) {
  if(!valid(id) || !owner_valid(owner))return ESP_ERR_INVALID_ARG;
  std::lock_guard<std::mutex> lock(mutex);
  if(!owners[id][0])return ESP_OK;
  if(std::strcmp(owners[id],owner)!=0)return ESP_ERR_INVALID_STATE;
  owners[id][0]=0;return ESP_OK;
}
bool bsp_v2_resource_is_acquired(bsp_v2_resource_t id) {
  if(!valid(id))return false;
  std::lock_guard<std::mutex> lock(mutex);return owners[id][0]!=0;
}
bool bsp_v2_resource_contains_pin(bsp_v2_resource_t id,int gpio) {
  if(!valid(id) || gpio<0 || gpio>=64)return false;
  const auto &d=lunaredu::bsp_v2::resources()[id];
  return d.available && (d.pin_mask & (uint64_t{1}<<gpio));
}
bool bsp_v2_resource_observe(bsp_v2_resource_t id,bsp_v2_resource_observation_t *out) {
  if(!valid(id) || !out)return false;
  std::lock_guard<std::mutex> lock(mutex);const auto &d=lunaredu::bsp_v2::resources()[id];
  *out={};out->name=d.name;out->configured_available=d.available;out->pin_mask=d.pin_mask;
  std::strcpy(out->owner,owners[id]);return true;
}
size_t bsp_v2_resource_signals(bsp_v2_resource_t id,bsp_v2_signal_t *out,size_t capacity) {
  if(!valid(id) || !lunaredu::bsp_v2::resources()[id].available)return 0;
  return lunaredu::bsp_v2::resource_signals(id,out,capacity);
}
