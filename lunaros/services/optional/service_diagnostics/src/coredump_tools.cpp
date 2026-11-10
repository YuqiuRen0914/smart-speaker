#include "coredump_tools.h"
#if CONFIG_LUNAROS_DIAGNOSTIC_COREDUMP_FLASH
#include "diagnostic_outbox.h"
#include "lunaros_storage.h"
#include "lunaros_platform_link.h"
#include "configuration/device_config.h"
#include "lunaros_digest.h"
#include "kernel/static_mutex.h"
#include <ArduinoJson.h>
#include <esp_core_dump.h>
#include <esp_app_desc.h>
#include <esp_partition.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <mbedtls/base64.h>
#include <array>
#include <algorithm>
#include <memory>
#include <cstdio>
#include <cstring>
namespace lunaredu::lunaros::internal::coredump_tools {
namespace {
StaticMutex mutex;bool ready=false;uint64_t next_poll=0;std::string retained;
constexpr const char *marker="/_lunaros_private/diagnostics/coredump-owner.json";
std::string fingerprint(){char text[17];std::snprintf(text,sizeof(text),"%016llx",static_cast<unsigned long long>(device_cloud_credential_fingerprint()));return text;}
struct Capture {const esp_partition_t *partition=nullptr;size_t offset=0,size=0;std::string elf;};
bool prepare_capture(Capture &capture){
 size_t address=0,size=0;if(esp_core_dump_image_check()!=ESP_OK||esp_core_dump_image_get(&address,&size)!=ESP_OK)return false;
 const auto *part=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_COREDUMP,"coredump");
 if(!part||!size||size>0x80000||address<part->address||address-part->address>part->size||size>part->size-(address-part->address))return false;
 std::unique_ptr<esp_core_dump_summary_t,decltype(&heap_caps_free)> summary(static_cast<esp_core_dump_summary_t*>(heap_caps_calloc(1,sizeof(esp_core_dump_summary_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)),heap_caps_free);
 if(!summary||esp_core_dump_get_summary(summary.get())!=ESP_OK)return false;
 const auto *identity=reinterpret_cast<const char*>(summary->app_elf_sha256);
 if(sizeof(summary->app_elf_sha256)!=65||::strnlen(identity,65)!=64||std::strspn(identity,"0123456789abcdef")!=64)return false;
 capture={part,address-part->address,size,identity};return true;
}
bool stream_capture(diagnostic_outbox::CaptureWriter writer,void *context,void *opaque,std::string &error){
 const auto &capture=*static_cast<Capture*>(opaque);
 const std::string header="ELF file SHA256: "+capture.elf+"\n================= CORE DUMP START =================\n";
 if(!writer(reinterpret_cast<const uint8_t*>(header.data()),header.size(),context))return false;
 std::array<uint8_t,192> raw{};std::array<uint8_t,260> encoded{};
 for(size_t offset=0;offset<capture.size;){const auto count=std::min(raw.size(),capture.size-offset);size_t written=0;
  if(esp_partition_read(capture.partition,capture.offset+offset,raw.data(),count)!=ESP_OK||mbedtls_base64_encode(encoded.data(),encoded.size(),&written,raw.data(),count)!=0){error="coredump_read_failed";return false;}
  encoded[written++]='\n';
  if(!writer(encoded.data(),written,context))return false;
  offset+=count;
 }
 constexpr const char footer[]="================= CORE DUMP END =================\n";
 return writer(reinterpret_cast<const uint8_t*>(footer),sizeof(footer)-1,context);
}
bool capture_digest(Capture &capture,std::string &result,size_t &size){
 struct HashWriter {digest::Sha256 hash;size_t count=0;} state;
 if(!state.hash.begin())return false;
 const auto writer=[](const uint8_t *bytes,size_t count,void *opaque){auto &out=*static_cast<HashWriter*>(opaque);out.count+=count;return out.hash.update(bytes,count);};
 std::string error;std::array<uint8_t,32> sha{};
 if(!stream_capture(writer,&state,&capture,error)||!state.hash.finish(sha))return false;
 result=digest::hex(sha.data(),sha.size());size=state.count;return true;
}
void record_current_owner(){
 const auto binding=platform_link_binding_status();const auto credential=fingerprint();
 if(binding.binding_epoch<=0||binding.revocation_requested||binding.persistence_pending||!device_cloud_credential_fingerprint())return;
 const auto *app=esp_app_get_description();const auto elf=digest::hex(app->app_elf_sha256,32);
 JsonDocument meta;meta["epoch"]=binding.binding_epoch;meta["credential"]=credential;meta["elf"]=elf;
 std::string text;serializeJson(meta,text);std::string previous;
 if(storage_read_text_file_bounded(marker,512,previous)&&previous==text)return;
 const auto current=platform_link_binding_status();if(current.binding_epoch!=binding.binding_epoch||current.revocation_requested||current.persistence_pending||fingerprint()!=credential)return;
 storage_write_text_file(marker,text.c_str());
}
}
void resume(){StaticLockGuard lock(mutex);ready=true;next_poll=0;retained.clear();}
void shutdown(){StaticLockGuard lock(mutex);ready=false;retained.clear();}
void poll(){
 StaticLockGuard lock(mutex);const auto now=uint64_t(esp_timer_get_time()/1000);
 if(!ready||now<next_poll||!storage_ready())return;
 next_poll=now+60000;
 const auto check=esp_core_dump_image_check();
 if(check==ESP_ERR_NOT_FOUND){record_current_owner();return;}
 // A corrupt or unowned dump is quarantined; never relabel it as this boot.
 if(check!=ESP_OK)return;
 Capture capture;if(!prepare_capture(capture))return;
 std::string identity;JsonDocument meta;if(!storage_read_text_file_bounded(marker,512,identity)||deserializeJson(meta,identity))return;
 const auto binding=platform_link_binding_status();const auto captured_credential=device_cloud_credential_fingerprint();
 if(binding.binding_epoch<=0||binding.revocation_requested||binding.persistence_pending||!meta["epoch"].is<int>()||meta["epoch"].as<int>()!=binding.binding_epoch||meta["credential"].as<std::string>()!=fingerprint()||meta["elf"].as<std::string>()!=capture.elf)return;
 std::string sha;size_t size=0;if(!capture_digest(capture,sha,size)||sha==retained)return;
 std::string error;if(diagnostic_outbox::enqueue_stream("coredump",capture.elf.c_str(),sha.c_str(),size,stream_capture,&capture,{binding.binding_epoch,captured_credential},error))retained=sha;
}
bool retire_acknowledged_capture(const char *expected){
 StaticLockGuard lock(mutex);
 if(!ready)return false;
 const auto check=esp_core_dump_image_check();if(check==ESP_ERR_NOT_FOUND)return true;if(check!=ESP_OK)return false;
 Capture capture;std::string actual;size_t size=0;
 if(!expected||!prepare_capture(capture)||!capture_digest(capture,actual,size))return false;
 // A later crash may have replaced the partition before an older ACK arrives.
 if(actual!=expected)return true;
 if(esp_core_dump_image_erase()!=ESP_OK)return false;
 retained.clear();next_poll=0;return true;
}
}
#endif
