#include "insights_tools.h"
#if CONFIG_LUNAROS_DIAGNOSTIC_INSIGHTS
#include "diagnostic_outbox.h"
#include "lunaros_platform_link.h"
#include "configuration/device_config.h"
#include "lunaros_network_jobs.h"
#if LUNAROS_DIAGNOSTICS_AUDIO && !CONFIG_LUNAROS_DIAGNOSTIC_INSIGHTS_IO
#include "lunaros_audio_bridge.h"
#endif
#include <array>
#include <cstdio>
#include <string>
#include <memory>
#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
extern "C" {
#include <esp_diagnostics_metrics.h>
#include <esp_diag_data_store.h>
#include <esp_insights_encoder.h>
}
namespace lunaredu::lunaros::internal::insights_tools {
namespace {
bool initialized=false;uint64_t next_sample=0;
#if CONFIG_LUNAROS_DIAGNOSTIC_INSIGHTS_IO
constexpr const char *keys[]{"net_queue_wait_max_ms","net_run_max_ms","net_submitted","net_completed",
  "net_retried","net_cancelled","net_deadlines","net_rejected_capacity"};
#else
constexpr const char *keys[]={"internal_free","internal_minimum","internal_largest","psram_free","tasks","net_queue_wait_max_ms"
#if LUNAROS_DIAGNOSTICS_AUDIO
  ,"pcm_queue_timeouts","pcm_reference_drops"
#endif
};
#endif
static_assert(std::size(keys)<=8);
struct EncodingWorkspace {
 std::array<uint8_t,2048> raw;
 std::array<uint8_t,8192> output;
};
esp_err_t write_metric(const char *tag,void *data,size_t size,void*){return esp_diag_data_store_non_critical_write(tag,data,size);}
}
void shutdown(){if(!initialized)return;esp_diag_metrics_deinit();esp_diag_data_store_deinit();initialized=false;next_sample=0;}
void resume(){
 if(initialized)return;
 if(esp_diag_data_store_init()!=ESP_OK)return;
 esp_diag_metrics_config_t config{write_metric,nullptr};
 if(esp_diag_metrics_init(&config)!=ESP_OK){esp_diag_data_store_deinit();return;}
 initialized=true;
 for(const auto *key:keys)if(esp_diag_metrics_register("lunaros",key,key,"system",ESP_DIAG_DATA_TYPE_UINT)!=ESP_OK){shutdown();return;}
}
void poll(){
 if(!initialized)return;
 const auto now=uint64_t(esp_timer_get_time()/1000);if(now<next_sample)return;next_sample=now+900000;
 const auto binding=platform_link_binding_status();const auto credential=device_cloud_credential_fingerprint();
 if(binding.state!=PlatformLinkBindingState::Bound||binding.binding_epoch<=0||binding.revocation_requested||binding.persistence_pending||!credential)return;
 // The retained diagnostic I/O job is the only sampling/encoding owner.
 // Keep the 10 KiB workspace off both Main and Network task stacks. It exists
 // only for this sample; allocation failure retries without dropping records.
#if CONFIG_SPIRAM
 constexpr auto caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
#else
 constexpr auto caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
#endif
 std::unique_ptr<EncodingWorkspace,decltype(&heap_caps_free)> workspace(
   static_cast<EncodingWorkspace*>(heap_caps_calloc(1,sizeof(EncodingWorkspace),caps)),heap_caps_free);
 if(!workspace){next_sample=now+60000;return;}
 esp_diag_data_discard_data();
 const auto network=network_job_metrics();
#if LUNAROS_DIAGNOSTICS_AUDIO && !CONFIG_LUNAROS_DIAGNOSTIC_INSIGHTS_IO
 const auto pcm=audio::local_pcm_audio_bridge_status();
#endif
#if CONFIG_LUNAROS_DIAGNOSTIC_INSIGHTS_IO
 const uint32_t values[]{network.maximum_queue_wait_ms,network.maximum_run_elapsed_ms,
   network.submitted,network.completed,network.retried,network.cancelled,network.deadline_exceeded,network.rejected_capacity};
#else
 const uint32_t values[]={uint32_t(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),uint32_t(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),uint32_t(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),uint32_t(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),uint32_t(uxTaskGetNumberOfTasks()),network.maximum_queue_wait_ms
#if LUNAROS_DIAGNOSTICS_AUDIO
  ,pcm.output_queue_timeouts,pcm.reference_drops
#endif
 };
#endif
 static_assert(std::size(values)==std::size(keys));
 for(size_t i=0;i<std::size(keys);++i){
#if LUNAROS_DIAGNOSTICS_AUDIO && !CONFIG_LUNAROS_DIAGNOSTIC_INSIGHTS_IO
   if(i>=6&&!pcm.initialized)continue; // An absent bridge has no zero-valued audio observation.
#endif
   if(esp_diag_metrics_report_uint("lunaros",keys[i],values[i])!=ESP_OK)return;
 }
 auto &raw=workspace->raw;auto &output=workspace->output;
 const int count=esp_diag_data_store_non_critical_read(raw.data(),raw.size());if(count<=0||size_t(count)>raw.size())return;
 const auto *app=esp_app_get_description();
 const auto meta_size=esp_insights_encode_meta(output.data(),4096,reinterpret_cast<char*>(const_cast<uint8_t*>(app->app_elf_sha256)));
 if(meta_size<=3||meta_size>4096)return;
 if(esp_insights_encode_data_begin(output.data()+meta_size,output.size()-meta_size)!=ESP_OK)return;
 esp_insights_encode_boottime_data();
 const auto consumed=esp_insights_encode_non_critical_data(raw.data(),size_t(count));
 const auto data_size=esp_insights_encode_data_end(output.data()+meta_size);
 if(!consumed||consumed>size_t(count)||data_size<=3||data_size>output.size()-meta_size)return;
 const auto current=platform_link_binding_status();
 if(current.state!=PlatformLinkBindingState::Bound||current.binding_epoch!=binding.binding_epoch||current.revocation_requested||current.persistence_pending||device_cloud_credential_fingerprint()!=credential){esp_diag_data_discard_data();return;}
 char elf[65]{};for(size_t i=0;i<32;++i)std::snprintf(elf+2*i,3,"%02x",app->app_elf_sha256[i]);
 std::string error;
 if(diagnostic_outbox::enqueue("insights",elf,output.data(),meta_size+data_size,{binding.binding_epoch,credential},error))esp_diag_data_store_non_critical_release(consumed);
 else next_sample=now+60000; // Retry collection only after a bounded backoff.
}
}
#endif
