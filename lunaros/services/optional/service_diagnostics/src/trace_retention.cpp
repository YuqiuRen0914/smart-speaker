#include "trace_retention.h"
#if CONFIG_LUNAROS_DIAGNOSTIC_FIELD && (CONFIG_LUNAROS_DIAGNOSTIC_APPTRACE || CONFIG_LUNAROS_DIAGNOSTIC_SYSTEMVIEW)
#include "diagnostic_outbox.h"
#include "kernel/static_mutex.h"
#include "lunaros_platform_link.h"
#include "configuration/device_config.h"
#include <esp_heap_caps.h>
#include <esp_app_desc.h>
#include <esp_timer.h>
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <cstring>
#include <cstdio>
#if CONFIG_LUNAROS_DIAGNOSTIC_SYSTEMVIEW
#include <esp_trace_registry.h>
#endif

namespace lunaredu::lunaros::internal::trace_retention {
namespace {
constexpr size_t kCapacity=32*1024;
constexpr size_t kStagingCapacity=4096;
StaticMutex control;
portMUX_TYPE gate=portMUX_INITIALIZER_UNLOCKED;
uint8_t *buffer=nullptr;
uint8_t *staging=nullptr;
size_t head=0,tail=0,staged=0,archived=0;
size_t used=0;
bool capturing=false,pending=false,exporting=false,truncated=false;
uint32_t dropped=0,retry_at=0;
uint64_t deadline=0;
const char *kind=nullptr;
diagnostic_outbox::CaptureAuthority authority;
std::string last_result;
uint32_t now(){return uint32_t(uint64_t(esp_timer_get_time())/1000);}
void release_locked(){
  // Only detached buffers reach here. Shutdown is called after retained I/O
  // retirement, and collect owns exporting until enqueue returns.
  heap_caps_free(buffer);buffer=nullptr;pending=false;used=0;
  heap_caps_free(staging);staging=nullptr;staged=0;archived=0;
}
// Control owner only. Never touch PSRAM with the ISR gate held. A bounded
// internal stack copy lets writers append while the existing Main loop drains.
void drain_locked(){
  uint8_t chunk[320];
  size_t budget=kStagingCapacity;
  while(budget){
    portENTER_CRITICAL(&gate);
    size_t length=staged;
    if(length>sizeof(chunk))length=sizeof(chunk);
    if(length>budget)length=budget;
    const size_t first=length<kStagingCapacity-tail?length:kStagingCapacity-tail;
    if(first)std::memcpy(chunk,staging+tail,first);
    if(length>first)std::memcpy(chunk+first,staging,length-first);
    tail=(tail+length)%kStagingCapacity;staged-=length;
    portEXIT_CRITICAL(&gate);
    if(!length)break;
    std::memcpy(buffer+archived,chunk,length);archived+=length;budget-=length;
  }
}
}
bool begin(const char *capture_kind,std::string &error,uint64_t deadline_ms){
  StaticLockGuard lock(control);
  if(buffer||exporting){error="trace_retention_pending";return false;}
  const auto binding=platform_link_binding_status();
  const auto credential=device_cloud_credential_fingerprint();
  if(binding.state!=PlatformLinkBindingState::Bound||binding.binding_epoch<=0||
      binding.revocation_requested||binding.persistence_pending||!credential){
    error="trace_retention_binding_required";return false;
  }
  auto *allocated=static_cast<uint8_t*>(heap_caps_calloc(1,kCapacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!allocated){error="trace_retention_no_external_memory";return false;}
  auto *stage=static_cast<uint8_t*>(heap_caps_calloc(1,kStagingCapacity,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  if(!stage){heap_caps_free(allocated);error="trace_retention_no_internal_memory";return false;}
  kind=capture_kind;authority={binding.binding_epoch,credential};retry_at=now();last_result.clear();
  portENTER_CRITICAL(&gate);
  buffer=allocated;staging=stage;head=tail=staged=archived=0;
  used=0;dropped=0;truncated=false;pending=false;
  deadline=deadline_ms>UINT64_MAX/1000?UINT64_MAX:deadline_ms*1000;capturing=true;
  portEXIT_CRITICAL(&gate);
  error.clear();return true;
}
bool IRAM_ATTR write(const void *bytes,size_t size){
  bool accepted=true;
  portENTER_CRITICAL_SAFE(&gate);
  // No 64-bit division helper (potentially in flash) on the ISR/cache-off path.
  if(capturing&&size&&uint64_t(esp_timer_get_time())<deadline){
    if(!bytes||size>320||truncated||size>kCapacity-used||size>kStagingCapacity-staged){
      truncated=true;if(dropped!=UINT32_MAX)++dropped;accepted=false;
    }else{
      const size_t first=size<kStagingCapacity-head?size:kStagingCapacity-head;
      std::memcpy(staging+head,bytes,first);
      if(size>first)std::memcpy(staging,static_cast<const uint8_t*>(bytes)+first,size-first);
      head=(head+size)%kStagingCapacity;staged+=size;used+=size;
    }
  }
  portEXIT_CRITICAL_SAFE(&gate);
  return accepted;
}
void finish(){
  StaticLockGuard lock(control);
  portENTER_CRITICAL(&gate);
  capturing=false;pending=buffer!=nullptr;
  portEXIT_CRITICAL(&gate);
  drain_locked();
  heap_caps_free(staging);staging=nullptr;
}
void drain(){StaticLockGuard lock(control);if(buffer&&!pending&&!exporting)drain_locked();}
bool saturated(){portENTER_CRITICAL(&gate);const bool value=capturing&&truncated;portEXIT_CRITICAL(&gate);return value;}
void collect(){
  const uint8_t *data=nullptr;size_t length=0;
  diagnostic_outbox::CaptureAuthority captured;
  const char *capture_kind=nullptr;
  {
    StaticLockGuard lock(control);
    if(!pending||exporting||int32_t(now()-retry_at)<0)return;
    if(!used){release_locked();last_result="trace_empty";return;}
    const auto binding=platform_link_binding_status();
    if(binding.state!=PlatformLinkBindingState::Bound||binding.revocation_requested||binding.persistence_pending||
        binding.binding_epoch!=authority.binding_epoch||device_cloud_credential_fingerprint()!=authority.credential_fingerprint){
      release_locked();last_result="trace_authority_changed";return;
    }
    exporting=true;retry_at=now()+60000;data=buffer;length=used;captured=authority;capture_kind=kind;
  }
  char elf[65]{};const auto *app=esp_app_get_description();
  for(size_t i=0;i<32;++i)std::snprintf(elf+2*i,3,"%02x",app->app_elf_sha256[i]);
  std::string error;
  const bool stored=diagnostic_outbox::enqueue(capture_kind,elf,data,length,captured,error);
  StaticLockGuard lock(control);exporting=false;
  last_result=stored?(truncated?"trace_prefix_persisted":"trace_persisted"):error;
  if(stored)release_locked();
}
bool shutdown(std::string &error){
  StaticLockGuard lock(control);
  if(exporting){error="trace_retention_io_active";return false;}
  portENTER_CRITICAL(&gate);capturing=false;portEXIT_CRITICAL(&gate);
  // A detached pending buffer survives service suspension. Resume's existing
  // I/O collector retries it; no callback or background task still owns it.
  if(!pending||!used)release_locked();
  error.clear();return true;
}
Status status(){
  StaticLockGuard lock(control);Status result;
  result.configured=true;result.capacity=kCapacity;result.last_result=last_result;
  result.staging_capacity=kStagingCapacity;
  portENTER_CRITICAL(&gate);
  result.capturing=capturing;result.pending=pending;result.bytes=used;
  result.truncated=truncated;result.dropped_chunks=dropped;
  result.staged_bytes=staged;
  portEXIT_CRITICAL(&gate);
  return result;
}
}

#if CONFIG_LUNAROS_DIAGNOSTIC_SYSTEMVIEW
namespace {
// The vendor encoder's DEBUG_PROBE format retains its dual-core event tags.
// Output consists of the actual SEGGER event bytes, without apptrace's shared
// memory block headers. The transport consumes no host commands.
uint32_t transport_config[3]{};
esp_err_t IRAM_ATTR init(esp_trace_transport_t*,const void*){return ESP_OK;}
esp_err_t IRAM_ATTR set_config(esp_trace_transport_t*,esp_trace_transport_cfg_key_t key,const void *value){
  if(!value||unsigned(key)>=3)return ESP_ERR_INVALID_ARG;
  transport_config[unsigned(key)]=*static_cast<const uint32_t*>(value);return ESP_OK;
}
esp_err_t IRAM_ATTR get_config(esp_trace_transport_t*,esp_trace_transport_cfg_key_t key,void *value){
  if(!value||unsigned(key)>=3)return ESP_ERR_INVALID_ARG;
  *static_cast<uint32_t*>(value)=transport_config[unsigned(key)];return ESP_OK;
}
esp_err_t IRAM_ATTR read(esp_trace_transport_t*,void*,size_t *size,uint32_t){if(!size)return ESP_ERR_INVALID_ARG;*size=0;return ESP_OK;}
esp_err_t IRAM_ATTR write(esp_trace_transport_t*,const void *data,size_t size,uint32_t){
  // Consume a full block even after saturation so vendor retry buffering never
  // contaminates the next capture. The retention snapshot counts the loss.
  (void)lunaredu::lunaros::internal::trace_retention::write(data,size);return ESP_OK;
}
esp_err_t IRAM_ATTR flush(esp_trace_transport_t*){return ESP_OK;}
esp_err_t IRAM_ATTR down(esp_trace_transport_t*,uint8_t*,uint32_t){return ESP_OK;}
bool IRAM_ATTR connected(esp_trace_transport_t*){return false;}
esp_trace_link_types_t IRAM_ATTR link_type(esp_trace_transport_t*){return ESP_TRACE_LINK_DEBUG_PROBE;}
void IRAM_ATTR panic(esp_trace_transport_t*,const void*){}
// The SDK dereferences this table with caches disabled as well as in ISRs.
DRAM_ATTR const esp_trace_transport_vtable_t transport={init,set_config,get_config,read,write,flush,down,connected,link_type,panic};
ESP_TRACE_REGISTER_TRANSPORT("lunar_retained",&transport);
}
#endif
#endif
