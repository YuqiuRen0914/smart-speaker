#include "diagnostic_outbox.h"
#include "trace_retention.h"
#include "sdkconfig.h"
#if CONFIG_LUNAROS_DIAGNOSTIC_FIELD
#include "lunaros_platform_link_diagnostics.h"
#include "lunaros_platform_link.h"
#include "lunaros_network_jobs.h"
#include "lunaros_diagnostics.h"
#include "lunaros_storage.h"
#include "configuration/device_config.h"
#include "lunaros_digest.h"
#include "heap_tools.h"
#include "coredump_tools.h"
#include "gprof_tools.h"
#include "insights_tools.h"
#include <ArduinoJson.h>
#include <esp_timer.h>
#include <array>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <atomic>
namespace lunaredu::lunaros::internal::diagnostic_outbox {
namespace {
constexpr size_t slots=6,limit=2*1024*1024;
constexpr const char *root="/_lunaros_private/diagnostics";
std::mutex mutex;
std::atomic<bool> ready{false};
// Files, slot occupancy and producer polls have one owner: the Network I/O
// callback. The lifecycle mutex protects only its retained job handle.
bool spool_initialized=false;
NetworkJobHandle job;
uint32_t next_attempt=0;
uint32_t stored_heap_generation=0;
Status observed;
std::array<bool,slots> occupied{};
std::string filename(size_t index,const char *extension){char text[96];std::snprintf(text,sizeof(text),"%s/slot-%u.%s",root,unsigned(index),extension);return text;}
bool initialize_spool_locked(){
 if(spool_initialized)return true;
 if(!storage_ready()||!storage_ensure_directory("/_lunaros_private")||!storage_ensure_directory(root))return false;
 occupied.fill(false);
 for(size_t i=0;i<slots;++i)occupied[i]=storage_path_exists(filename(i,"bin").c_str())||storage_path_exists(filename(i,"json").c_str())||storage_path_exists(filename(i,"ack").c_str());
 spool_initialized=true;return true;
}
uint32_t now(){return uint32_t(esp_timer_get_time()/1000);}
std::string fingerprint(){char text[17];std::snprintf(text,sizeof(text),"%016llx",static_cast<unsigned long long>(device_cloud_credential_fingerprint()));return text;}
bool valid_digest(const char *text){return text&&std::strlen(text)==64&&std::strspn(text,"0123456789abcdef")==64;}
bool valid_kind(const char *kind){
 if(!kind)return false;
 for(const auto *allowed:{"heaptrace","gprof","insights","coredump","apptrace","systemview"})
  if(!std::strcmp(kind,allowed))return true;
 return false;
}
bool current_authority(int epoch,const std::string &credential){
 const auto binding=platform_link_binding_status();
 return ready.load()&&binding.state==PlatformLinkBindingState::Bound&&
   binding.binding_epoch==epoch&&!binding.revocation_requested&&
   !binding.persistence_pending&&fingerprint()==credential;
}
bool persisted_capture_matches(size_t index,const char *expected,size_t size,int epoch,const std::string &credential){
 // Metadata is a commit record, not proof that its data file survived. Retry
 // must retain the new capture if a previous record was lost or corrupted.
 struct Verify {digest::Sha256 hash;size_t size,read=0;int epoch;const std::string &credential;} state{{},size,0,epoch,credential};
 if(!state.hash.begin())return false;
 const auto consume=[](const uint8_t *bytes,size_t count,void *context){
  auto &stream=*static_cast<Verify*>(context);
  if(!current_authority(stream.epoch,stream.credential)||count>stream.size-stream.read||!stream.hash.update(bytes,count))return false;
  stream.read+=count;return true;
 };
 // Storage pins the open path, streams with its existing bounded workspace,
 // and keeps its filesystem mutex out of callbacks and authority checks.
 if(!storage_stream_file(filename(index,"bin").c_str(),consume,&state)||state.read!=size)return false;
 std::array<uint8_t,32> bytes{};if(!state.hash.finish(bytes)||!current_authority(epoch,credential))return false;
 char hex[65]{};for(size_t i=0;i<bytes.size();++i)std::snprintf(hex+2*i,3,"%02x",bytes[i]);
 return std::strcmp(hex,expected)==0;
}
struct CaptureStream {
 CaptureProducer producer;void *context;size_t expected_size,written=0;
 const char *expected_hash;int epoch;std::string credential;
 digest::Sha256 hash;StorageFileChunkWriter writer=nullptr;void *writer_context=nullptr;
};
bool write_capture(const uint8_t *bytes,size_t count,void *context){
 auto &stream=*static_cast<CaptureStream*>(context);
 if(!current_authority(stream.epoch,stream.credential)||(!bytes&&count)||count>stream.expected_size-stream.written)return false;
 if(!stream.hash.update(bytes,count)||!stream.writer(bytes,count,stream.writer_context))return false;
 stream.written+=count;return true;
}
bool produce_capture(StorageFileChunkWriter writer,void *writer_context,void *context,std::string &error){
 auto &stream=*static_cast<CaptureStream*>(context);stream.writer=writer;stream.writer_context=writer_context;
 if(!stream.hash.begin()||!stream.producer(write_capture,&stream,stream.context,error)||stream.written!=stream.expected_size){error="diagnostic_capture_stream_failed";return false;}
 std::array<uint8_t,32> result{};if(!stream.hash.finish(result)){error="diagnostic_hash_failed";return false;}
 char hex[65]{};for(size_t i=0;i<result.size();++i)std::snprintf(hex+2*i,3,"%02x",result[i]);
 if(std::strcmp(hex,stream.expected_hash)||!current_authority(stream.epoch,stream.credential)){error="diagnostic_capture_changed";return false;}
 return true;
}
struct MemoryCapture {const uint8_t *bytes;size_t size;};
bool produce_memory(CaptureWriter writer,void *writer_context,void *context,std::string &){
 const auto &memory=*static_cast<MemoryCapture*>(context);
 for(size_t offset=0;offset<memory.size;){const auto count=std::min(size_t(2048),memory.size-offset);if(!writer(memory.bytes+offset,count,writer_context))return false;offset+=count;}
 return true;
}
bool cleanup_acknowledged(size_t index){
 std::string ack;if(!storage_read_text_file_bounded(filename(index,"ack").c_str(),64,ack)||!valid_digest(ack.c_str()))return false;
 std::string metadata;
 if(storage_path_exists(filename(index,"json").c_str())){
  JsonDocument meta;if(!storage_read_text_file_bounded(filename(index,"json").c_str(),2048,metadata)||deserializeJson(meta,metadata)||meta["sha256"].as<std::string>()!=ack)return false;
  if(!std::strcmp(meta["kind"]|"","coredump")&&!coredump_tools::retire_acknowledged_capture(ack.c_str()))return false;
 }
 for(const auto *extension:{"bin","json"})if(storage_path_exists(filename(index,extension).c_str())&&!storage_delete_file(filename(index,extension).c_str()))return false;
 return storage_delete_file(filename(index,"ack").c_str());
}
struct Reader {std::string path;size_t offset=0,size=0;NetworkJobHandle handle;int epoch=0;std::string credential;};
int read(uint8_t *bytes,size_t capacity,void *context){
 auto &reader=*static_cast<Reader*>(context);
 const auto binding=platform_link_binding_status();
 if(!ready.load()||network_job_cancellation_requested(reader.handle)||binding.state!=PlatformLinkBindingState::Bound||binding.binding_epoch!=reader.epoch||binding.revocation_requested||binding.persistence_pending||fingerprint()!=reader.credential)return -1;
 const auto count=std::min(capacity,reader.size-reader.offset);
 if(!count)return 0;
 if(!storage_read_range(reader.path.c_str(),reader.offset,bytes,count))return -1;
 reader.offset+=count;return int(count);
}
void collect_heap(){
 JsonDocument status;heap_tools::status(status.to<JsonObject>());
 const auto heap_generation=status["generation"].as<uint32_t>();
 if(!status["active"].as<bool>() && heap_generation && heap_generation!=stored_heap_generation){
  JsonDocument snapshot;std::string error;
  if(heap_tools::read(snapshot.to<JsonObject>(),error)){
   std::string bytes;serializeJson(snapshot,bytes);CaptureAuthority authority;
   if(heap_tools::upload_authority_for_generation(snapshot["generation"].as<uint32_t>(),authority.binding_epoch,authority.credential_fingerprint)&&
      enqueue("heaptrace",snapshot["elf_sha256"]|"",reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size(),authority,error))
    stored_heap_generation=heap_generation;
  }
 }
}
NetworkJobRunResult run_io(NetworkJobHandle handle,void *){
 if(!ready.load()||network_job_cancellation_requested(handle))return {false,false,"diagnostic_stopped"};
 if(!initialize_spool_locked())return {false,false,"diagnostic_storage_unavailable"};
 for(size_t i=0;i<slots;++i)if(cleanup_acknowledged(i))occupied[i]=false;
 // Collection and all filesystem work use the existing blocking I/O owner.
 // Main only submits/polls this retained job and advances SDK stop deadlines.
 collect_heap();
 if(!ready.load()||network_job_cancellation_requested(handle))return {false,false,"diagnostic_stopped"};
 trace_retention::collect();gprof_tools::collect();coredump_tools::poll();insights_tools::poll();
 if(!ready.load()||network_job_cancellation_requested(handle))return {false,false,"diagnostic_stopped"};
 if(std::none_of(occupied.begin(),occupied.end(),[](bool value){return value;}))return {true,false,"diagnostic_empty"};
 bool enabled=false;int epoch=0;
 if(!platform_link_diagnostic_policy(enabled,epoch)||!enabled)return {false,false,"diagnostic_upload_not_authorized"};
 JsonDocument meta;
 const auto credential=fingerprint();
 bool corrupt=false;
 for(size_t i=0;i<slots;++i){
  std::string metadata;if(!storage_read_text_file_bounded(filename(i,"json").c_str(),2048,metadata)||deserializeJson(meta,metadata))continue;
  if(!valid_kind(meta["kind"]|"")||!meta["bytes"].is<size_t>()||!meta["bytes"].as<size_t>()||meta["bytes"].as<size_t>()>limit||!meta["epoch"].is<int>()||meta["epoch"].as<int>()!=epoch||!valid_digest(meta["elf"]|"")||!valid_digest(meta["sha256"]|"")||meta["credential"].as<std::string>()!=credential)continue;
  // Never retransmit an acknowledged record while its cleanup is pending.
  if(storage_path_exists(filename(i,"ack").c_str()))continue;
  Reader reader{filename(i,"bin"),0,meta["bytes"].as<size_t>(),handle,epoch,credential};
 // Re-verify persisted bytes before transmission. Interrupted/modified files
 // are never associated with the originally recorded firmware identity.
 digest::Sha256 hash;std::array<uint8_t,2048> chunk{};
 if(!hash.begin())return {false,false,"diagnostic_hash_failed"};
 bool readable=true;
 while(reader.offset<reader.size){const auto count=read(chunk.data(),chunk.size(),&reader);if(count<=0||!hash.update(chunk.data(),size_t(count))){readable=false;break;}}
 if(!current_authority(epoch,credential)||network_job_cancellation_requested(handle))return {false,false,"diagnostic_upload_retired"};
 if(!readable){corrupt=true;continue;}
 std::array<uint8_t,32> bytes{};
 if(!hash.finish(bytes))return {false,false,"diagnostic_hash_failed"};
 char hex[65]{};for(size_t i=0;i<bytes.size();++i)std::snprintf(hex+2*i,3,"%02x",bytes[i]);
 if(std::strcmp(hex,meta["sha256"]|"")){corrupt=true;continue;}
 reader.offset=0;NetworkResponse response;
 if(!platform_link_upload_diagnostic(meta["kind"]|"",meta["elf"]|"",hex,{reader.size,read,&reader},response))return {false,false,"diagnostic_upload_unconfirmed"};
 if(network_job_cancellation_requested(handle)||!current_authority(epoch,credential))return {false,false,"diagnostic_upload_retired"};
 if(!storage_write_text_file(filename(i,"ack").c_str(),hex)||!cleanup_acknowledged(i))return {false,false,"diagnostic_ack_cleanup_pending"};
 occupied[i]=false;
 return {true,false,"diagnostic_uploaded"};
 }
 return {!corrupt,false,corrupt?"diagnostic_capture_corrupt":"diagnostic_empty_or_quarantined"};
}
NetworkJobRunResult run(NetworkJobHandle handle,void *context){
 auto result=run_io(handle,context);
 std::lock_guard<std::mutex> lock(mutex);
 observed.retained_records=uint32_t(std::count(occupied.begin(),occupied.end(),true));
 observed.inventory_known=spool_initialized;
 observed.last_attempt_ms=now();observed.last_result=result.detail;
 return result;
}
}
Status status(){
 std::lock_guard<std::mutex> lock(mutex);auto result=observed;
 result.configured=true;result.accepting=ready.load();
 result.io_active=bool(job)&&network_job_active(job);return result;
}
void resume(){
 std::lock_guard<std::mutex> lock(mutex);
 if(job&&network_job_active(job))return;
 ready=true;spool_initialized=false;observed.inventory_known=false;next_attempt=now();
}
void poll(){
 std::unique_lock<std::mutex> lock(mutex);
 if(job){if(network_job_active(job))return;std::string error;if(!release_network_job(job,error))return;job={};}
 if(!ready.load()||int32_t(now()-next_attempt)<0||!storage_ready())return;
 next_attempt=now()+60000;
 const auto binding=platform_link_binding_status();
 if(binding.state!=PlatformLinkBindingState::Bound||binding.revocation_requested||binding.persistence_pending)return;
 NetworkJobDefinition definition;
 definition.owner_service_id=kDiagnosticsServiceId;definition.kind="diagnostic_upload";
 definition.priority=kNetworkPriorityBackground;definition.deadline_ms=now()+30000;
 definition.runner=run;definition.retain_terminal=true;
 std::string error;
 if(!submit_network_job(definition,job,error)){
  observed.last_attempt_ms=now();observed.last_result="diagnostic_job_not_admitted";
 }
}
bool shutdown(std::string &error){
 std::lock_guard<std::mutex> lock(mutex);ready=false;
 if(job){cancel_network_job(job,error);if(network_job_active(job)){error="diagnostic_upload_retiring";return false;}if(!release_network_job(job,error))return false;job={};}
 error.clear();return true;
}
bool enqueue(const char *kind,const char *elf,const uint8_t *bytes,size_t size,const CaptureAuthority &authority,std::string &error){
 if(!bytes||!size||size>limit){error="diagnostic_capture_not_admitted";return false;}
 digest::Sha256 hash;std::array<uint8_t,32> result{};
 if(!hash.begin()||!hash.update(bytes,size)||!hash.finish(result)){error="diagnostic_hash_failed";return false;}
 char hex[65]{};for(size_t i=0;i<result.size();++i)std::snprintf(hex+2*i,3,"%02x",result[i]);
 MemoryCapture memory{bytes,size};
 return enqueue_stream(kind,elf,hex,size,produce_memory,&memory,authority,error);
}
bool enqueue_stream(const char *kind,const char *elf,const char *hex,size_t size,
    CaptureProducer producer,void *context,const CaptureAuthority &authority,std::string &error){
 // Only the retained I/O runner calls enqueue. Main never owns a filesystem
 // mutex or waits for a producer to finish writing a capture.
 const auto binding=platform_link_binding_status();
 if(!ready.load()||!initialize_spool_locked()||!valid_kind(kind)||!valid_digest(elf)||!valid_digest(hex)||!producer||!size||size>limit||binding.state!=PlatformLinkBindingState::Bound||binding.revocation_requested||binding.persistence_pending||authority.binding_epoch!=binding.binding_epoch||!authority.credential_fingerprint||authority.credential_fingerprint!=device_cloud_credential_fingerprint()){error="diagnostic_capture_not_admitted";return false;}
 // Deduplicate persisted captures before reserving another slot after reboot.
 for(size_t i=0;i<slots;++i)if(occupied[i]){std::string text;JsonDocument existing;
  if(storage_read_text_file_bounded(filename(i,"json").c_str(),2048,text)&&!deserializeJson(existing,text)&&existing["kind"].as<std::string>()==kind&&existing["elf"].as<std::string>()==elf&&existing["sha256"].as<std::string>()==hex&&existing["bytes"].is<size_t>()&&existing["bytes"].as<size_t>()==size&&existing["epoch"].as<int>()==authority.binding_epoch&&existing["credential"].as<std::string>()==fingerprint()&&persisted_capture_matches(i,hex,size,authority.binding_epoch,existing["credential"].as<std::string>())){error.clear();return true;}
 }
 size_t index=slots;for(size_t i=0;i<slots;++i)if(!occupied[i]){index=i;break;}
 if(index==slots){error="diagnostic_outbox_full";return false;}
 JsonDocument meta;meta["kind"]=kind;meta["elf"]=elf;meta["sha256"]=hex;meta["bytes"]=size;meta["epoch"]=authority.binding_epoch;char captured_fingerprint[17];std::snprintf(captured_fingerprint,sizeof(captured_fingerprint),"%016llx",static_cast<unsigned long long>(authority.credential_fingerprint));meta["credential"]=captured_fingerprint;
 std::string encoded;serializeJson(meta,encoded);
 CaptureStream stream{producer,context,size,0,hex,authority.binding_epoch,captured_fingerprint,{},nullptr,nullptr};
 if(!storage_write_streamed_file(filename(index,"bin").c_str(),produce_capture,&stream,nullptr,&error))return false;
 occupied[index]=true;
 if(!storage_write_text_file(filename(index,"json").c_str(),encoded.c_str())){
  if(storage_delete_file(filename(index,"bin").c_str()))occupied[index]=false;
  error="diagnostic_spool_metadata_failed";return false;
 }
 error.clear();return true;
}
}
#else
namespace lunaredu::lunaros::internal::diagnostic_outbox {
void resume(){}void poll(){}bool shutdown(std::string &error){error.clear();return true;}
bool enqueue(const char*,const char*,const uint8_t*,size_t,const CaptureAuthority&,std::string &error){error="diagnostic_field_not_configured";return false;}
bool enqueue_stream(const char*,const char*,const char*,size_t,CaptureProducer,void*,const CaptureAuthority&,std::string &error){error="diagnostic_field_not_configured";return false;}
}
#endif
