#include "bsp/boards/general/v2.h"
#include <cstdio>
#include <mutex>
namespace {std::mutex mutex;uint64_t next_lease=0;}
esp_err_t bsp_v2_audio_claim(const int *pins,size_t count,bsp_v2_audio_claim_t *out) {
  if(!pins || !count || !out)return ESP_ERR_INVALID_ARG;
  if(!bsp_v2_board())return ESP_ERR_NOT_SUPPORTED;
  std::lock_guard<std::mutex> lock(mutex);
  if(out->lease_id)return ESP_ERR_INVALID_STATE;
  uint64_t mask=0;
  for(size_t i=0;i<count;++i) {
    if(pins[i]==-1)continue;
    if(!bsp_v2_resource_contains_pin(BSP_V2_RESOURCE_AUDIO,pins[i]) ||
       (mask & (uint64_t{1}<<pins[i])))return ESP_ERR_INVALID_ARG;
    mask|=uint64_t{1}<<pins[i];
  }
  if(!mask)return ESP_ERR_INVALID_ARG;
  if(next_lease==UINT64_MAX)return ESP_ERR_NO_MEM;
  const uint64_t id=++next_lease;char owner[BSP_V2_OWNER_MAX];
  std::snprintf(owner,sizeof(owner),"v2.audio:%llu",static_cast<unsigned long long>(id));
  const auto e=bsp_v2_resource_acquire(BSP_V2_RESOURCE_AUDIO,owner);
  if(e==ESP_OK)out->lease_id=id;
  return e;
}
esp_err_t bsp_v2_audio_release(bsp_v2_audio_claim_t *claim) {
  if(!claim)return ESP_ERR_INVALID_ARG;
  std::lock_guard<std::mutex> lock(mutex);
  if(!claim->lease_id)return ESP_OK;
  char owner[BSP_V2_OWNER_MAX];
  std::snprintf(owner,sizeof(owner),"v2.audio:%llu",static_cast<unsigned long long>(claim->lease_id));
  const auto e=bsp_v2_resource_release(BSP_V2_RESOURCE_AUDIO,owner);
  if(e==ESP_OK)claim->lease_id=0;
  return e;
}
