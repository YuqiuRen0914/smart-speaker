#include "espnow_ota.h"
#if CONFIG_LUNAREDU_NETWORK_ENABLE_ESPNOW && CONFIG_LUNAROS_NETWORK_ESPNOW_MANAGEMENT
#include "lunaredu_ota.h"
#include "lunaros_network_espnow_management.h"
#include "espnow_ota_transfer.h"
#include "kernel/static_mutex.h"
#include <array>
#include <cstring>
#include <cstdio>
#include <atomic>
namespace lunaredu::lunaros::internal {
namespace {
StaticMutex mutex;
std::atomic<bool> admitted{false};
struct Intent {bool queued=false;uint32_t generation=0,size=0;std::array<char,65> sha{};} pending;
uint8_t last_result=0; // 0=accepted/idle, 1=authority expired, 2=install rejected.
uint32_t get32(const uint8_t *in){uint32_t value=0;for(unsigned i=0;i<4;++i)value|=uint32_t(in[i])<<(8*i);return value;}
void put32(uint8_t *out,uint32_t value){for(unsigned i=0;i<4;++i)out[i]=uint8_t(value>>(8*i));}
bool handle(const EspNowManagementRequest &request,uint8_t *output,size_t &size,std::string &error){
  if(!admitted.load()){error="espnow_ota_service_stopped";return false;}
  if(!request.data||!request.size||size<46)return false;
  if(request.data[0]>=3)return handle_espnow_ota_transfer(request.data,request.size,output,size,error);
  if(request.data[0]==2){
    // Apply only an exact, already verified official release. A radio packet
    // cannot supply a URL, downgrade flag, raw image or Boot/recovery bypass.
    if(request.size!=37)return false;
    Intent intent;intent.queued=true;intent.generation=espnow_management_status().generation;intent.size=get32(request.data+33);
    for(size_t i=0;i<32;++i)std::snprintf(intent.sha.data()+2*i,3,"%02x",request.data[1+i]);
    const auto update=ota_service_available_update_snapshot();
    if(!update.available||update.package_size!=intent.size||update.package_sha256!=intent.sha.data()){
      error="espnow_ota_release_mismatch";return false;
    }
    StaticLockGuard lock(mutex);if(pending.queued){error="espnow_ota_intent_busy";return false;}
    pending=intent;last_result=0;
  }else if(request.data[0]!=1||request.size!=1)return false;
  const auto facts=ota_service_status_snapshot();const auto update=ota_service_available_update_snapshot();
  output[0]=0;output[1]=1;
  {StaticLockGuard lock(mutex);output[2]=pending.queued;output[3]=last_result;}
  output[4]=facts.upload_active||facts.pull_active||facts.pull_queued;output[5]=facts.restart_pending;
  output[6]=facts.has_result;output[7]=facts.success;output[8]=facts.pull_progress_percent;output[9]=update.available;
  put32(output+10,uint32_t(update.package_size));
  // No URL, cloud grant, credentials or filesystem contents leave the owner.
  const auto hex=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
  std::memset(output+14,0,32);
  if(update.available&&update.package_sha256.size()==64)for(size_t i=0;i<32;++i){
    const auto high=hex(update.package_sha256[2*i]),low=hex(update.package_sha256[2*i+1]);
    if(high<0||low<0)return false;
    output[14+i]=uint8_t((high<<4)|low);
  }
  size=46;return true;
}
}
bool bind_espnow_ota(){const bool bound=espnow_bind_management_provider(EspNowManagementFamily::Ota,handle);admitted.store(bound);return bound;}
bool espnow_ota_admitted(){return admitted.load();}
bool stop_espnow_ota(std::string &error){
  admitted.store(false);
  {StaticLockGuard lock(mutex);pending={};}
  // Advance both owners each step, even while the other is still retiring.
  std::string sender_error,transfer_error;
  const bool sender=stop_espnow_ota_sender(sender_error),transfer=stop_espnow_ota_transfer(transfer_error);
  if(!sender||!transfer){error=!sender?sender_error:transfer_error;return false;}
  error.clear();return true;
}
void poll_espnow_ota(){
  if(!admitted.load()){std::string error;(void)stop_espnow_ota(error);return;}
  poll_espnow_ota_transfer();
  poll_espnow_ota_sender();
  Intent intent;{StaticLockGuard lock(mutex);if(!pending.queued)return;intent=pending;pending={};}
  const auto authority=espnow_management_status();uint8_t result=1;
  if(authority.active&&authority.authenticated&&authority.generation==intent.generation&&(authority.families&8)){
    std::string error;result=ota_service_apply_available_update(intent.sha.data(),intent.size,error)?0:2;
  }
  StaticLockGuard lock(mutex);last_result=result;
}
}
#endif
