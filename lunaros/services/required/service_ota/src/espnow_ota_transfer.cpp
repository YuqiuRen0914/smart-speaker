#include "sdkconfig.h"
#include "espnow_ota_transfer.h"
#include "espnow_ota.h"
#include "lunaros_ota_espnow.h"
#include "kernel/static_mutex.h"
#include "kernel/registration_storage.h"

#if CONFIG_LUNAREDU_NETWORK_ENABLE_ESPNOW && CONFIG_LUNAROS_NETWORK_ESPNOW_MANAGEMENT && LUNAROS_ESPNOW_OTA_STORAGE
#include "lunaredu_ota.h"
#include "lunaros_network_espnow_management.h"
#include "lunaros_network_jobs.h"
#include "lunaros_storage.h"
#include "lunaros_digest.h"
#include <lunar_espnow_ota_wire.h>
#include <esp_timer.h>
#ifdef ESP_PLATFORM
#include <esp_attr.h>
#endif
#include <array>
#include <cstring>
#include <limits>
#include <algorithm>
#include <cstddef>

namespace lunaredu::lunaros {
namespace {
constexpr const char *staged_path="/_lunaros_private/ota/espnow.bin";
constexpr uint32_t maximum_image=CONFIG_ESPNOW_OTA_FIRMWARE_SIZE_MAX;
static_assert(ESPNOW_OTA_PACKET_MAX_SIZE==208 && sizeof(espnow_ota_status_t)==30);
static_assert(sizeof(espnow_ota_packet_t)+5<=218);
StaticMutex transfer_mutex;
enum class Work {None,Begin,Append,Verify,Install,Cleanup};
struct Transfer {
  OtaEspNowTransferStatus status;
  Work work=Work::None;
  StorageWriteHandle writer;
  NetworkJobHandle job;
  digest::Sha256 hash;
  RegistrationArray<uint8_t> chunk{true};
  size_t chunk_size=0;
  std::array<char,65> sha{};
  OtaAvailableUpdate release;
  bool cancelled=false,published=false,install_committed=false,cleaned=true;
  OtaEspNowTransferPhase terminal=OtaEspNowTransferPhase::Idle;
  uint64_t deadline=0,next_cleanup=0;
};
#if defined(ESP_PLATFORM) && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
EXT_RAM_BSS_ATTR
#endif
Transfer transfer;
uint64_t now(){return uint64_t(esp_timer_get_time())/1000;}
uint32_t get32(const uint8_t *data){uint32_t result=0;for(unsigned i=0;i<4;++i)result|=uint32_t(data[i])<<(i*8);return result;}
void put32(uint8_t *data,uint32_t value){for(unsigned i=0;i<4;++i)data[i]=uint8_t(value>>(i*8));}
bool current_authority(uint32_t generation){
  const auto authority=espnow_management_status();
  return authority.active&&authority.authenticated&&authority.generation==generation&&(authority.families&8);
}
bool release_matches(const OtaAvailableUpdate &release){
  const auto update=ota_service_available_update_snapshot();
  return update.available&&update.package_sha256==release.package_sha256&&update.package_size==release.package_size&&
    update.role_aware==release.role_aware&&(!update.role_aware||
      (update.role_request.app_elf_sha256==release.role_request.app_elf_sha256&&update.role_request.target_role==release.role_request.target_role&&
       update.role_request.project_name==release.role_request.project_name&&update.role_request.version==release.role_request.version&&
       update.role_request.layout_id==release.role_request.layout_id&&update.role_request.hardware_id==release.role_request.hardware_id&&
       update.role_request.sha256==release.role_request.sha256&&update.role_request.size==release.role_request.size));
}
bool cancelled(){StaticLockGuard lock(transfer_mutex);return transfer.cancelled||now()>=transfer.deadline;}
bool cleanup(std::string &error){
  if(!storage_abort_write(transfer.writer,error))return false;
  if(transfer.published||storage_path_exists(staged_path)){
    if(!storage_delete_file(staged_path)){error="espnow_ota_staging_cleanup_failed";return false;}
  }
  transfer.published=false;
  if(!transfer.hash.abort()){error="espnow_ota_hash_retiring";return false;}
  return true;
}
NetworkJobRunResult run(NetworkJobHandle handle,void*){
  Work work;OtaEspNowTransferStatus status;
  {StaticLockGuard lock(transfer_mutex);work=transfer.work;status=transfer.status;}
  std::string error;bool ok=false;
  const bool admitted=work==Work::Cleanup||(!cancelled()&&current_authority(status.generation));
  if(!admitted)error="espnow_ota_authority_expired";
  else if(work==Work::Begin){
    ok=storage_ready()&&cleanup(error)&&transfer.hash.begin()&&storage_begin_write(staged_path,status.expected_bytes,transfer.writer,error);
  }else if(work==Work::Append){
    ok=storage_append_write(transfer.writer,status.persisted_bytes,transfer.chunk.data(),transfer.chunk_size,error)&&
      transfer.hash.update(transfer.chunk.data(),transfer.chunk_size);
  }else if(work==Work::Verify){
    digest::Sha256Value hash{};
    ok=status.persisted_bytes==status.expected_bytes&&transfer.hash.finish(hash)&&
      digest::hex(hash.data(),hash.size())==transfer.sha.data()&&current_authority(status.generation)&&!cancelled();
    if(ok){ok=storage_commit_write(transfer.writer,error);if(ok)transfer.published=true;}
    if(!ok&&error.empty())error="espnow_ota_package_digest_mismatch";
  }else if(work==Work::Install){
    // All radio bytes are persisted before maintenance closes ESP-NOW. The
    // immutable file is pinned while the existing installer consumes it.
    // Authorization is committed once, immediately before that transition.
    if(!transfer.published||!release_matches(transfer.release))error="espnow_ota_release_changed";
    else if(transfer.release.role_aware&&!ota_cloud_request_allowed(transfer.release.role_request,ota_service_cloud_inventory(),error)){}
    else {
      struct Source {size_t offset=0,total=0;digest::Sha256 hash;std::array<char,65> expected{};bool valid=true;};
      Source source;source.total=status.expected_bytes;source.expected=transfer.sha;
      bool commit=false;
      if(source.hash.begin()&&current_authority(status.generation)){
        StaticLockGuard lock(transfer_mutex);
        commit=internal::espnow_ota_admitted()&&!transfer.cancelled&&now()<transfer.deadline&&transfer.status.phase==OtaEspNowTransferPhase::Installing;
        if(commit)transfer.install_committed=true;
      }
      if(commit){
        OtaFirmwareStream stream;stream.content_length=source.total;stream.context=&source;
        stream.expected_sha256=source.expected.data();stream.maintenance_authorized=true;
        stream.role_request=transfer.release.role_aware?&transfer.release.role_request:nullptr;
        stream.bootstrap_update=stream.role_request&&stream.role_request->target_role=="boot";
        stream.read_chunk=[](uint8_t *output,size_t capacity,void *context){
          auto &source=*static_cast<Source*>(context);const auto count=std::min(capacity,source.total-source.offset);
          if(!source.valid||!count||!storage_read_range(staged_path,source.offset,output,count)||!source.hash.update(output,count)){source.valid=false;return -1;}
          source.offset+=count;return int(count);
        };
        stream.validate=[](void *context){auto &source=*static_cast<Source*>(context);digest::Sha256Value value{};
          return source.valid&&source.offset==source.total&&source.hash.finish(value)&&digest::hex(value.data(),value.size())==source.expected.data();};
        // Storage's streaming lease pins this exact path for the whole
        // installation; the callback itself performs no nested file writes.
        struct Install {OtaFirmwareStream *stream;};
        Install install{&stream};
        const auto pin=[](size_t bytes,void *opaque,std::string &error){auto &install=*static_cast<Install*>(opaque);
          if(bytes!=install.stream->content_length){error="espnow_ota_staging_size_changed";return false;}
          return ota_service_install_stream(*install.stream,error);};
        ok=storage_with_pinned_file(staged_path,pin,&install,error);
      }else error="espnow_ota_install_authority_expired";
    }
  }else if(work==Work::Cleanup)ok=cleanup(error);
  else error="espnow_ota_no_work";
  const bool stale=work!=Work::Install&&work!=Work::Cleanup&&(!current_authority(status.generation)||cancelled()||network_job_cancellation_requested(handle));
  if(stale){ok=false;error="espnow_ota_authority_expired";}
  {
    StaticLockGuard lock(transfer_mutex);transfer.work=Work::None;transfer.status.job_active=false;
    if(ok){
      if(work==Work::Begin)transfer.status.phase=OtaEspNowTransferPhase::Receiving;
      if(work==Work::Append){transfer.status.persisted_bytes+=uint32_t(transfer.chunk_size);transfer.status.phase=OtaEspNowTransferPhase::Receiving;}
      if(work==Work::Verify)transfer.status.phase=OtaEspNowTransferPhase::Ready;
      if(work==Work::Install)transfer.status.phase=transfer.terminal=OtaEspNowTransferPhase::Completed;
      if(work==Work::Cleanup){transfer.status.phase=transfer.terminal;transfer.cleaned=true;transfer.cancelled=false;transfer.install_committed=false;transfer.release={};transfer.sha.fill(0);}
    }else{transfer.status.phase=work==Work::Cleanup?OtaEspNowTransferPhase::Retiring:OtaEspNowTransferPhase::Failed;
      if(work!=Work::Cleanup)transfer.terminal=OtaEspNowTransferPhase::Failed;
      transfer.next_cleanup=now()+1000;transfer.status.last_error=error.empty()?"espnow_ota_io_failed":error;}
    transfer.chunk.fill(0);transfer.chunk_size=0;
    if(transfer.cleaned)transfer.chunk.clear();
  }
  return {ok,false,error};
}
bool queue(Work work,std::string &error){
  // Called with the transfer mutex. No callback executes inline on submit.
  if(transfer.work!=Work::None||network_job_active(transfer.job)){error="espnow_ota_transfer_busy";return false;}
  NetworkJobDefinition job;job.owner_service_id=kUpdateServiceId;job.kind="espnow.firmware";
  job.priority=kNetworkPriorityControl;job.runner=run;job.maximum_attempts=1;
  job.job_class=work==Work::Cleanup?NetworkJobClass::Retirement:NetworkJobClass::Normal;
  transfer.work=work;transfer.status.job_active=true;
  if(!submit_network_job(job,transfer.job,error)){transfer.work=Work::None;transfer.status.job_active=false;return false;}
  return true;
}
void encode_status(uint8_t *output,size_t &size){
  output[0]=0;output[1]=2;output[2]=uint8_t(transfer.status.phase);output[3]=transfer.status.job_active;
  put32(output+4,transfer.status.id);put32(output+8,transfer.status.expected_bytes);
  put32(output+12,transfer.status.received_bytes);put32(output+16,transfer.status.persisted_bytes);size=20;
}
bool encode_official_status(uint8_t page,uint8_t *output,size_t &size,std::string &error){
  constexpr size_t bits_per_page=ESPNOW_OTA_PROGRESS_MAX_SIZE*8;
  const auto packets=(transfer.status.expected_bytes+ESPNOW_OTA_PACKET_MAX_SIZE-1)/ESPNOW_OTA_PACKET_MAX_SIZE;
  if(size<sizeof(espnow_ota_status_t)+ESPNOW_OTA_PROGRESS_MAX_SIZE||!packets||size_t(page)*bits_per_page>=packets){
    error="espnow_ota_progress_page_invalid";return false;
  }
  espnow_ota_status_t status{};status.type=ESPNOW_OTA_TYPE_STATUS;
  digest::Sha256Value sha{};
  if(!digest::decode_hex(transfer.sha.data(),sha.data(),sha.size())){error="espnow_ota_progress_unavailable";return false;}
  std::memcpy(status.sha_256,sha.data(),ESPNOW_OTA_HASH_LEN);
  status.packet_num=uint16_t(packets);status.total_size=transfer.status.expected_bytes;
  status.written_size=transfer.status.persisted_bytes;status.progress_index=page;
  if(transfer.status.phase==OtaEspNowTransferPhase::Preparing)status.error_code=ESP_ERR_ESPNOW_OTA_FIRMWARE_NOT_INIT;
  else if(transfer.status.phase==OtaEspNowTransferPhase::Failed||transfer.status.phase==OtaEspNowTransferPhase::Retiring)
    status.error_code=ESP_ERR_ESPNOW_OTA_FIRMWARE_INVALID;
  else if(transfer.status.phase==OtaEspNowTransferPhase::Ready||transfer.status.phase==OtaEspNowTransferPhase::Completed)
    status.error_code=ESP_ERR_ESPNOW_OTA_FIRMWARE_FINISH;
  std::array<uint8_t,ESPNOW_OTA_PROGRESS_MAX_SIZE> bitmap{};
  // Only durable, complete packets are advertised. A final short packet is
  // complete only once the exact expected byte count has been persisted.
  const auto durable=status.written_size==status.total_size?packets:status.written_size/ESPNOW_OTA_PACKET_MAX_SIZE;
  for(size_t i=0;i<bits_per_page&&size_t(page)*bits_per_page+i<durable;++i)ESPNOW_OTA_SET_BITS(bitmap.data(),i);
  std::memcpy(output,&status,sizeof(status));std::memcpy(output+sizeof(status),bitmap.data(),bitmap.size());
  size=sizeof(status)+bitmap.size();error.clear();return true;
}
}
OtaEspNowCapabilities ota_service_espnow_capabilities(){return {true,true,true,maximum_image,ESPNOW_OTA_PACKET_MAX_SIZE,ESPNOW_OTA_PROGRESS_MAX_SIZE};}
OtaEspNowTransferStatus ota_service_espnow_transfer_status(){StaticLockGuard lock(transfer_mutex);auto result=transfer.status;
  result.job_active=network_job_active(transfer.job)||transfer.work!=Work::None;return result;}
bool internal::handle_espnow_ota_transfer(const uint8_t *data,size_t length,uint8_t *output,size_t &size,std::string &error){
  if(!data||!length||!output||size<20)return false;
  const auto authority=espnow_management_status();
  if(!authority.active||!authority.authenticated||!(authority.families&8)){error="espnow_ota_authority_required";return false;}
  const auto release=data[0]==3?ota_service_available_update_snapshot():OtaAvailableUpdate{};
  StaticLockGuard lock(transfer_mutex);
  if(data[0]==8&&length==1){encode_status(output,size);return true;}
  if(data[0]==3){
    if(length!=37||!transfer.cleaned||network_job_active(transfer.job)||transfer.work!=Work::None){error="espnow_ota_transfer_not_idle";return false;}
    const auto expected=get32(data+1);const auto sha=digest::hex(data+5,32);
    if(!expected||expected>maximum_image||!release.available||release.package_size!=expected||release.package_sha256!=sha){error="espnow_ota_release_mismatch";return false;}
    // Installation must be admitted by the same role-aware policy, not by a
    // radio-provided role/address or a recovery bypass.
    if(release.role_aware&&(release.role_request.sha256!=sha||release.role_request.size!=expected)){error="espnow_ota_role_mismatch";return false;}
    if(transfer.status.id==std::numeric_limits<uint32_t>::max()){error="espnow_ota_transfer_id_exhausted";return false;}
    if(!transfer.chunk.resize(209)){error="espnow_ota_chunk_allocation_failed";return false;}
    const auto id=transfer.status.id+1;transfer.status={};transfer.status.id=id;transfer.status.generation=authority.generation;
    transfer.status.expected_bytes=expected;transfer.release=release;std::memcpy(transfer.sha.data(),sha.c_str(),65);
    transfer.deadline=authority.expires_at_ms;transfer.cancelled=false;transfer.install_committed=false;transfer.cleaned=false;transfer.next_cleanup=0;transfer.terminal=OtaEspNowTransferPhase::Idle;
    if(!queue(Work::Begin,error)){transfer.status.phase=OtaEspNowTransferPhase::Idle;transfer.cleaned=true;transfer.release={};transfer.sha.fill(0);transfer.chunk.clear();return false;}
    transfer.status.phase=OtaEspNowTransferPhase::Preparing;
  }else {
    if(length<5||get32(data+1)!=transfer.status.id||transfer.status.generation!=authority.generation){error="espnow_ota_transfer_stale";return false;}
    if(data[0]==10&&length==6){return encode_official_status(data[5],output,size,error);
    }else if(data[0]==9){
      constexpr size_t header=offsetof(espnow_ota_packet_t,data);
      espnow_ota_packet_t packet{};
      if(length<5+header||length>5+sizeof(packet)){error="espnow_ota_official_packet_size";return false;}
      std::memcpy(&packet,data+5,length-5);
      const auto offset=uint32_t(packet.seq)*ESPNOW_OTA_PACKET_MAX_SIZE;
      if(packet.type!=ESPNOW_OTA_TYPE_DATA||!packet.size||packet.size>ESPNOW_OTA_PACKET_MAX_SIZE||
          length!=5+header+packet.size||transfer.status.phase!=OtaEspNowTransferPhase::Receiving||
          offset!=transfer.status.persisted_bytes||offset>=transfer.status.expected_bytes||
          packet.size!=std::min(uint32_t(ESPNOW_OTA_PACKET_MAX_SIZE),transfer.status.expected_bytes-offset)){
        error="espnow_ota_official_packet_sequence";return false;
      }
      if(transfer.work!=Work::None||network_job_active(transfer.job)){error="espnow_ota_transfer_busy";return false;}
      transfer.chunk_size=packet.size;std::memcpy(transfer.chunk.data(),packet.data,packet.size);
      if(!queue(Work::Append,error)){transfer.chunk.fill(0);transfer.chunk_size=0;return false;}
      transfer.status.received_bytes=offset+packet.size;transfer.status.phase=OtaEspNowTransferPhase::Writing;
    }else if(data[0]==4){
      if(length<10||length>218||transfer.status.phase!=OtaEspNowTransferPhase::Receiving||get32(data+5)!=transfer.status.persisted_bytes||
          length-9>transfer.status.expected_bytes-transfer.status.persisted_bytes){error="espnow_ota_fragment_offset_or_size";return false;}
      if(transfer.work!=Work::None||network_job_active(transfer.job)){error="espnow_ota_transfer_busy";return false;}
      transfer.chunk_size=length-9;std::memcpy(transfer.chunk.data(),data+9,transfer.chunk_size);
      if(!queue(Work::Append,error)){transfer.chunk.fill(0);transfer.chunk_size=0;return false;}
      transfer.status.received_bytes=transfer.status.persisted_bytes+uint32_t(length-9);transfer.status.phase=OtaEspNowTransferPhase::Writing;
    }else if(data[0]==5&&length==5){
      if(transfer.status.phase!=OtaEspNowTransferPhase::Receiving||transfer.status.persisted_bytes!=transfer.status.expected_bytes){error="espnow_ota_transfer_incomplete";return false;}
      if(!queue(Work::Verify,error))return false;
      transfer.status.phase=OtaEspNowTransferPhase::Verifying;
    }else if(data[0]==6&&length==5){
      if(transfer.status.phase!=OtaEspNowTransferPhase::Ready){error="espnow_ota_transfer_not_ready";return false;}
      if(!queue(Work::Install,error))return false;
      transfer.status.phase=OtaEspNowTransferPhase::Installing;
    }else if(data[0]==7&&length==5){
      if(transfer.status.phase==OtaEspNowTransferPhase::Installing){error="espnow_ota_install_committed";return false;}
      transfer.cancelled=true;transfer.terminal=OtaEspNowTransferPhase::Failed;transfer.status.last_error="espnow_ota_cancelled";transfer.status.phase=OtaEspNowTransferPhase::Retiring;
    }else return false;
  }
  encode_status(output,size);error.clear();return true;
}
void internal::poll_espnow_ota_transfer(){
  {StaticLockGuard lock(transfer_mutex);if(transfer.cleaned&&!network_job_active(transfer.job))return;}
  const auto authority=espnow_management_status();StaticLockGuard lock(transfer_mutex);
  const bool expired=!authority.active||!authority.authenticated||authority.generation!=transfer.status.generation||now()>=transfer.deadline;
  if(network_job_active(transfer.job)){
    if(transfer.work!=Work::Cleanup&&!transfer.install_committed&&(transfer.cancelled||expired)){
      std::string error;(void)cancel_network_job(transfer.job,error);
    }
    return;
  }
  if(transfer.work!=Work::None){
    // A queued job can be cancelled without entering its runner. Its native
    // buffers and Storage handle still belong to this owner until cleanup.
    transfer.work=Work::None;transfer.status.job_active=false;transfer.cancelled=true;
    transfer.status.phase=transfer.terminal=OtaEspNowTransferPhase::Failed;transfer.status.last_error="espnow_ota_job_cancelled_before_dispatch";
    transfer.chunk.fill(0);transfer.chunk_size=0;
  }
  if(transfer.cleaned||now()<transfer.next_cleanup)return;
  if(transfer.status.phase==OtaEspNowTransferPhase::Completed||transfer.status.phase==OtaEspNowTransferPhase::Failed||
      transfer.status.phase==OtaEspNowTransferPhase::Retiring||transfer.cancelled||
      (!transfer.install_committed&&expired)){
    if(transfer.terminal!=OtaEspNowTransferPhase::Completed){transfer.terminal=OtaEspNowTransferPhase::Failed;
      if(transfer.status.last_error.empty())transfer.status.last_error="espnow_ota_authority_expired";}
    std::string error;if(queue(Work::Cleanup,error))transfer.status.phase=OtaEspNowTransferPhase::Retiring;else transfer.next_cleanup=now()+1000;
  }
}
bool internal::stop_espnow_ota_transfer(std::string &error){
  {
    StaticLockGuard lock(transfer_mutex);
    if(transfer.cleaned&&!network_job_active(transfer.job)){error.clear();return true;}
    if(!transfer.install_committed){transfer.cancelled=true;transfer.terminal=OtaEspNowTransferPhase::Failed;
      transfer.status.last_error="espnow_ota_service_stopped";}
  }
  poll_espnow_ota_transfer();
  StaticLockGuard lock(transfer_mutex);
  if(!transfer.cleaned||network_job_active(transfer.job)){error="espnow_ota_transfer_retiring";return false;}
  error.clear();return true;
}
}
#else
namespace lunaredu::lunaros {
OtaEspNowCapabilities ota_service_espnow_capabilities(){return {};}
OtaEspNowTransferStatus ota_service_espnow_transfer_status(){return {};}
bool internal::handle_espnow_ota_transfer(const uint8_t *,size_t,uint8_t *,size_t &,std::string &error){error="espnow_ota_storage_unavailable";return false;}
void internal::poll_espnow_ota_transfer(){}
bool internal::stop_espnow_ota_transfer(std::string &error){error.clear();return true;}
}
#endif
