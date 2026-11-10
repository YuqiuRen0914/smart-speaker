#include "heap_tools.h"
#include "sdkconfig.h"
#if CONFIG_LUNAROS_DIAGNOSTIC_HEAP
#include "kernel/static_mutex.h"
#include <algorithm>
#include <cstdio>
#include <limits>
#include <esp_timer.h>
#if CONFIG_LUNAROS_DIAGNOSTIC_FIELD
#include "lunaros_platform_link.h"
#include "configuration/device_config.h"
#endif
#if CONFIG_LUNAROS_DIAGNOSTIC_HEAP
#include <esp_heap_trace.h>
#include <esp_heap_caps.h>
#include <esp_app_desc.h>
#endif
namespace lunaredu::lunaros::internal::heap_tools {
namespace {
StaticMutex mutex;
bool ready=false, active=false;
uint64_t deadline=0;
uint32_t generation=0;
int last_error=0;
#if CONFIG_LUNAROS_DIAGNOSTIC_FIELD
int captured_epoch=0;
uint64_t captured_credential=0;
#endif
#if CONFIG_LUNAROS_DIAGNOSTIC_HEAP
heap_trace_record_t *records=nullptr;
constexpr size_t capacity=256;
bool stop_locked(std::string &error) {
  deadline=0;
  if(active) {
    last_error=heap_trace_stop();
    if(last_error!=ESP_OK){error="heap_trace_stop_failed";return false;}
    active=false;
  }
  error.clear();return true;
}
#else
bool stop_locked(std::string &error){active=false;deadline=0;error.clear();return true;}
#endif
}
void resume(){StaticLockGuard lock(mutex);ready=true;}
bool stop(std::string &error){StaticLockGuard lock(mutex);return stop_locked(error);}
bool shutdown(std::string &error){
  StaticLockGuard lock(mutex);ready=false;
  if(!stop_locked(error))return false;
#if CONFIG_LUNAROS_DIAGNOSTIC_HEAP
  if(records){
    last_error=heap_trace_init_standalone(nullptr,0);
    if(last_error!=ESP_OK){error="heap_trace_detach_failed";return false;}
    heap_caps_free(records);records=nullptr;
  }
#endif
  return true;
}
bool start(uint32_t duration, bool leaks_only, uint64_t authorization_deadline, std::string &error){
  StaticLockGuard lock(mutex);
#if CONFIG_LUNAROS_DIAGNOSTIC_HEAP
  const auto now=uint64_t(esp_timer_get_time())/1000;
  if(!ready){error="heap_trace_service_stopped";return false;}
  if(active){error="heap_trace_busy";return false;}
  if(duration<100 || duration>900000 || now>=authorization_deadline){error="heap_trace_invalid_window";return false;}
  if(generation==std::numeric_limits<uint32_t>::max()){error="heap_trace_generation_exhausted";return false;}
  if(!records)records=static_cast<heap_trace_record_t*>(heap_caps_calloc(capacity,sizeof(heap_trace_record_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  if(!records){error="heap_trace_no_memory";return false;}
  last_error=heap_trace_init_standalone(records,capacity);
  if(last_error==ESP_OK)last_error=heap_trace_start(leaks_only?HEAP_TRACE_LEAKS:HEAP_TRACE_ALL);
  if(last_error!=ESP_OK){error="heap_trace_start_failed";return false;}
  active=true;deadline=std::min(now+duration,authorization_deadline);++generation;
#if CONFIG_LUNAROS_DIAGNOSTIC_FIELD
  const auto binding=platform_link_binding_status();
  captured_epoch=binding.state==PlatformLinkBindingState::Bound&&!binding.revocation_requested&&!binding.persistence_pending?binding.binding_epoch:0;
  captured_credential=device_cloud_credential_fingerprint();
#endif
  error.clear();return true;
#else
  (void)duration;(void)leaks_only;(void)authorization_deadline;
  error="heap_trace_not_configured";return false;
#endif
}
void poll(bool authorized){
  StaticLockGuard lock(mutex);
  if(active && (!authorized || uint64_t(esp_timer_get_time())/1000>=deadline)){
    std::string error;stop_locked(error);
  }
}
void status(JsonObject output){
  StaticLockGuard lock(mutex);
#if CONFIG_LUNAROS_DIAGNOSTIC_HEAP
  output["configured"]=true;
#else
  output["configured"]=false;
#endif
  output["active"]=active;output["generation"]=generation;output["last_sdk_error"]=last_error;
  const auto now=uint64_t(esp_timer_get_time())/1000;
  output["remaining_ms"]=active && deadline>now?deadline-now:0;
#if CONFIG_LUNAROS_DIAGNOSTIC_HEAP
  heap_trace_summary_t summary{};
  if(records && heap_trace_summary(&summary)==ESP_OK){
    output["count"]=summary.count;output["capacity"]=summary.capacity;
    output["overflow"]=bool(summary.has_overflowed);
    output["allocations"]=summary.total_allocations;output["frees"]=summary.total_frees;
  }
#endif
}
bool read(JsonObject output,std::string &error){
  StaticLockGuard lock(mutex);
#if CONFIG_LUNAROS_DIAGNOSTIC_HEAP
  if(active){error="heap_trace_stop_before_read";return false;}
  if(!records || generation==0){error="heap_trace_empty";return false;}
  heap_trace_summary_t summary{};
  if(heap_trace_summary(&summary)!=ESP_OK){error="heap_trace_summary_failed";return false;}
  output["format"]="LHT1";output["generation"]=generation;
  output["mode"]=summary.mode==HEAP_TRACE_LEAKS?"leaks":"all";
  output["overflow"]=bool(summary.has_overflowed);output["capacity"]=summary.capacity;
  output["allocations"]=summary.total_allocations;output["frees"]=summary.total_frees;
  const auto *app=esp_app_get_description();char elf[65]{};
  for(size_t i=0;i<32;++i)std::snprintf(elf+2*i,3,"%02x",app->app_elf_sha256[i]);
  output["elf_sha256"]=elf;
  auto list=output["records"].to<JsonArray>();
  const auto pointer=[](JsonVariant target,const void *address){char text[2+sizeof(uintptr_t)*2+1];std::snprintf(text,sizeof(text),"0x%lx",static_cast<unsigned long>(reinterpret_cast<uintptr_t>(address)));target.set(text);};
  for(size_t i=0;i<summary.count && i<capacity;++i){
    heap_trace_record_t record{};
    if(heap_trace_get(i,&record)!=ESP_OK){error="heap_trace_read_failed";return false;}
    auto row=list.add<JsonObject>();pointer(row["address"],record.address);
    row["size"]=record.size;row["freed"]=record.freed;row["cpu"]=record.ccount&1;
    auto alloc=row["allocated_by"].to<JsonArray>();auto freed=row["freed_by"].to<JsonArray>();
    for(size_t j=0;j<CONFIG_HEAP_TRACING_STACK_DEPTH;++j){
      if(record.alloced_by[j])pointer(alloc.add<JsonVariant>(),record.alloced_by[j]);
      if(record.freed_by[j])pointer(freed.add<JsonVariant>(),record.freed_by[j]);
    }
  }
  error.clear();return true;
#else
  (void)output;error="heap_trace_not_configured";return false;
#endif
}
bool upload_authority_for_generation(uint32_t expected_generation,int &epoch,uint64_t &credential){
#if CONFIG_LUNAROS_DIAGNOSTIC_FIELD
  StaticLockGuard lock(mutex);const auto binding=platform_link_binding_status();
  epoch=captured_epoch;credential=captured_credential;
  return expected_generation==generation && captured_epoch>0 && binding.state==PlatformLinkBindingState::Bound && !binding.revocation_requested && !binding.persistence_pending && binding.binding_epoch==captured_epoch && device_cloud_credential_fingerprint()==captured_credential;
#else
  (void)expected_generation;(void)epoch;(void)credential;return false;
#endif
}
}
#endif
