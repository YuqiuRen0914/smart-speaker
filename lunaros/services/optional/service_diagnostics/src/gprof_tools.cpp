#include "gprof_tools.h"
#include "sdkconfig.h"
#if CONFIG_GPROF_ENABLE
#include "kernel/static_mutex.h"
#include "diagnostic_outbox.h"
#include <gprof.h>
#include <lunar_gprof_port.h>
#include <esp_timer.h>
#include <esp_app_desc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/base64.h>
#include <atomic>
#include <algorithm>
#include <limits>
#include <cstdio>
#if CONFIG_LUNAROS_DIAGNOSTIC_FIELD
#include "lunaros_platform_link.h"
#include "configuration/device_config.h"
#endif
namespace lunaredu::lunaros::internal::gprof_tools {
namespace {
StaticMutex mutex;
bool ready=false,active=false,complete=false;
std::atomic<bool> stop_requested{false};
TaskHandle_t owner=nullptr;
uint64_t deadline=0,started=0;
uint32_t generation=0,elapsed=0;
int last_error=0;
#if CONFIG_LUNAROS_DIAGNOSTIC_FIELD
int captured_epoch=0;
uint64_t captured_credential=0;
uint32_t retry_store_at=0;
bool stored=false;
#endif
uint64_t now(){return uint64_t(esp_timer_get_time())/1000;}
bool snapshot(JsonObject output,std::string &error){
 if(active||!complete){error="gprof_snapshot_unavailable";return false;}
 size_t count=0;const auto *data=lunar_gprof_store_bytes(&count);
 if(!data||!count||count>65520){error="gprof_snapshot_invalid";return false;}
 std::string encoded((count+2)/3*4+1,'\0');size_t length=0;
 if(mbedtls_base64_encode(reinterpret_cast<unsigned char*>(encoded.data()),encoded.size(),&length,data,count)!=0){error="gprof_encode_failed";return false;}
 encoded.resize(length);output["format"]="LGP1";output["generation"]=generation;
 output["owner_task"]="main";output["window_ms"]=elapsed;output["overflow"]=lunar_gprof_overflowed();output["data"]=encoded;
 char elf[65]{};const auto *app=esp_app_get_description();for(size_t i=0;i<32;++i)std::snprintf(elf+2*i,3,"%02x",app->app_elf_sha256[i]);output["elf_sha256"]=elf;
 error.clear();return true;
}
bool stop_locked(std::string &error){
 lunar_gprof_set_active(false);
 if(!active){error.clear();return true;}
 if(xTaskGetCurrentTaskHandle()!=owner || lunar_gprof_inflight()) {error="gprof_retirement_pending";stop_requested.store(true);return false;}
 last_error=esp_gprof_save();
 if(last_error!=ESP_OK){error="gprof_save_failed";stop_requested.store(true);return false;}
 last_error=esp_gprof_deinit();
 if(last_error!=ESP_OK){error="gprof_deinit_failed";return false;}
 active=false;complete=true;stop_requested.store(false);elapsed=uint32_t(now()-started);owner=nullptr;
 error.clear();return true;
}
}
void resume(){StaticLockGuard lock(mutex);ready=true;}
void request_stop(){lunar_gprof_set_active(false);stop_requested.store(true);}
bool start(uint32_t duration,uint64_t authorization_deadline,std::string &error){
 StaticLockGuard lock(mutex);
 if(!ready||active){error="gprof_not_admitted";return false;}
 if(duration<100||duration>60000||now()>=authorization_deadline||generation==std::numeric_limits<uint32_t>::max()){error="gprof_invalid_window";return false;}
 lunar_gprof_set_active(false);if(lunar_gprof_inflight()){error="gprof_retirement_pending";return false;}
 lunar_gprof_store_release();complete=false;
 last_error=esp_gprof_init();
 if(last_error!=ESP_OK){lunar_gprof_store_release();error="gprof_init_failed";return false;}
 owner=xTaskGetCurrentTaskHandle();active=true;started=now();deadline=std::min(started+duration,authorization_deadline);++generation;stop_requested.store(false);
#if CONFIG_LUNAROS_DIAGNOSTIC_FIELD
 const auto binding=platform_link_binding_status();captured_epoch=binding.state==PlatformLinkBindingState::Bound&&!binding.revocation_requested&&!binding.persistence_pending?binding.binding_epoch:0;captured_credential=device_cloud_credential_fingerprint();stored=false;retry_store_at=0;
#endif
 lunar_gprof_set_deadline(uint32_t(deadline));lunar_gprof_set_active(true);error.clear();return true;
}
bool stop(std::string &error){StaticLockGuard lock(mutex);return stop_locked(error);}
void poll(bool authorized){
 StaticLockGuard lock(mutex);
 if(active&&(!authorized||stop_requested.load()||now()>=deadline)){std::string error;stop_locked(error);}
}
void collect(){
#if CONFIG_LUNAROS_DIAGNOSTIC_FIELD
 std::string text,elf;diagnostic_outbox::CaptureAuthority authority;
 uint32_t captured_generation=0;
 {
  StaticLockGuard lock(mutex);
  const auto current=uint32_t(now());
  if(!ready||!complete||stored||int32_t(current-retry_store_at)<0)return;
  retry_store_at=current+60000;const auto binding=platform_link_binding_status();
  if(captured_epoch<=0||binding.binding_epoch!=captured_epoch||binding.state!=PlatformLinkBindingState::Bound||binding.revocation_requested||binding.persistence_pending||device_cloud_credential_fingerprint()!=captured_credential)return;
  JsonDocument document;std::string error;
  if(!snapshot(document.to<JsonObject>(),error))return;
  serializeJson(document,text);elf=document["elf_sha256"].as<std::string>();
  authority={captured_epoch,captured_credential};captured_generation=generation;
 }
 // Storage never holds the profiler lock used by Main's stop/start path.
 std::string error;
 const bool persisted=diagnostic_outbox::enqueue("gprof",elf.c_str(),reinterpret_cast<const uint8_t*>(text.data()),text.size(),authority,error);
 StaticLockGuard lock(mutex);
 if(ready&&complete&&generation==captured_generation)stored=persisted;
#endif
}
bool shutdown(std::string &error){StaticLockGuard lock(mutex);ready=false;if(!stop_locked(error))return false;lunar_gprof_store_release();complete=false;return true;}
void status(JsonObject output){StaticLockGuard lock(mutex);output["configured"]=true;output["active"]=active&&!stop_requested.load();output["retiring"]=active&&stop_requested.load();output["generation"]=generation;output["last_sdk_error"]=last_error;output["owner_task"]="main";output["snapshot_ready"]=complete;output["overflow"]=lunar_gprof_overflowed();}
bool read(JsonObject output,std::string &error){StaticLockGuard lock(mutex);return snapshot(output,error);}
}
#endif
