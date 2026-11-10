#include "sdkconfig.h"
#include "espnow_ota_transfer.h"
#include "lunaros_ota_espnow.h"
#include "espnow_ota.h"

#if CONFIG_LUNAREDU_NETWORK_ENABLE_ESPNOW && CONFIG_LUNAROS_NETWORK_ESPNOW_MANAGEMENT && LUNAROS_ESPNOW_OTA_STORAGE
#include "lunaros_network_jobs.h"
#include "lunaros_network_espnow_client.h"
#include "lunaros_update_service_id.h"
#include "lunaros_storage.h"
#include "lunaros_digest.h"
#include <lunar_espnow_ota_wire.h>
#include "kernel/static_mutex.h"
#include "kernel/registration_storage.h"
#include <esp_timer.h>
#ifdef ESP_PLATFORM
#include <esp_attr.h>
#endif
#include <array>
#include <atomic>
#include <cstring>
#include <algorithm>
#include <limits>
#include <cstddef>

namespace lunaredu::lunaros {
namespace {
StaticMutex sender_mutex;
struct Sender {
  OtaEspNowSendStatus status;
  KernelRuntime *runtime=nullptr;
  bool (*authorized)(void*)=nullptr;
  void *context=nullptr;
  std::string path;
  digest::Sha256Value sha{};
  RegistrationArray<uint8_t> chunk{true};
  std::array<uint8_t,20> reply{};
  size_t chunk_size=0;
  NetworkJobHandle job;
  uint32_t client_generation=0;
  uint64_t next_poll=0,deadline=0;
  bool reply_ready=false,reply_invalid=false,read_ready=false,read_failed=false;
  bool client_owned=false,client_retired=false,opening=false,cancelled=false;
  uint8_t outstanding=0;
  uint8_t progress_page=0;
  uint32_t progress_bytes=0;
  bool progress_checked=false;
};
#if defined(ESP_PLATFORM) && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
EXT_RAM_BSS_ATTR
#endif
Sender sender;
uint64_t now(){return uint64_t(esp_timer_get_time())/1000;}
void put32(uint8_t *bytes,uint32_t value){for(unsigned i=0;i<4;++i)bytes[i]=uint8_t(value>>(8*i));}
uint32_t get32(const uint8_t *bytes){uint32_t value=0;for(unsigned i=0;i<4;++i)value|=uint32_t(bytes[i])<<(8*i);return value;}
bool authorized(void*){
  bool (*gate)(void*)=nullptr;void *context=nullptr;uint64_t deadline=0;bool cancelled=false;
  {StaticLockGuard lock(sender_mutex);gate=sender.authorized;context=sender.context;deadline=sender.deadline;cancelled=sender.cancelled;}
  return internal::espnow_ota_admitted()&&!cancelled&&now()<deadline&&gate&&gate(context);
}
void retired(void*){StaticLockGuard lock(sender_mutex);sender.client_retired=true;}
bool response_accepted(void*,EspNowManagementFamily family,const uint8_t *data,size_t size){
  if(!data||!size||family!=EspNowManagementFamily::Ota)return false;
  StaticLockGuard lock(sender_mutex);
  if(sender.outstanding!=10)return data[0]==0;
  espnow_ota_status_t status{};
  if(size!=sizeof(status)+ESPNOW_OTA_PROGRESS_MAX_SIZE)return false;
  std::memcpy(&status,data,sizeof(status));return status.type==ESPNOW_OTA_TYPE_STATUS&&!status.error_code;
}
void response(void*,uint8_t,EspNowManagementFamily family,uint32_t,const uint8_t *data,size_t size){
  StaticLockGuard lock(sender_mutex);
  if(!sender.status.active||family!=EspNowManagementFamily::Ota)return;
  if(sender.outstanding==10){
    espnow_ota_status_t status{};
    if(sender.reply_ready||!data||size!=sizeof(status)+ESPNOW_OTA_PROGRESS_MAX_SIZE){sender.reply_invalid=true;return;}
    std::memcpy(&status,data,sizeof(status));
    const auto total=sender.status.expected_bytes;
    const auto packets=(total+ESPNOW_OTA_PACKET_MAX_SIZE-1)/ESPNOW_OTA_PACKET_MAX_SIZE;
    if(status.type!=ESPNOW_OTA_TYPE_STATUS||status.error_code||status.total_size!=total||status.packet_num!=packets||
        status.progress_index!=sender.progress_page||status.written_size<sender.status.persisted_bytes||status.written_size>total||
        (status.written_size!=total&&status.written_size%ESPNOW_OTA_PACKET_MAX_SIZE)||
        std::memcmp(status.sha_256,sender.sha.data(),ESPNOW_OTA_HASH_LEN)){
      sender.reply_invalid=true;return;
    }
    const auto durable=status.written_size==total?packets:status.written_size/ESPNOW_OTA_PACKET_MAX_SIZE;
    for(size_t i=0;i<ESPNOW_OTA_PROGRESS_MAX_SIZE*8;++i){
      const bool expected=size_t(status.progress_index)*ESPNOW_OTA_PROGRESS_MAX_SIZE*8+i<durable;
      if(bool(ESPNOW_OTA_GET_BITS(data+sizeof(status),i))!=expected){sender.reply_invalid=true;return;}
    }
    // Preserve the OS lifecycle/id contract while consuming the official
    // packet count, durable-byte count and progress bitmap from the peer.
    sender.reply.fill(0);sender.reply[1]=2;sender.reply[2]=uint8_t(OtaEspNowTransferPhase::Receiving);
    put32(sender.reply.data()+4,sender.status.transfer_id);put32(sender.reply.data()+8,total);
    put32(sender.reply.data()+12,status.written_size);put32(sender.reply.data()+16,status.written_size);
    sender.reply_ready=true;return;
  }
  if(sender.reply_ready||!data||size!=sender.reply.size()||data[0]||data[1]!=2){sender.reply_invalid=true;return;}
  std::memcpy(sender.reply.data(),data,size);sender.reply_ready=true;
}
NetworkJobRunResult read(NetworkJobHandle handle,void*){
  std::string path;uint32_t offset=0,total=0;
  {StaticLockGuard lock(sender_mutex);path=sender.path;offset=sender.status.persisted_bytes;total=sender.status.expected_bytes;}
  size_t capacity=209;
  {StaticLockGuard lock(sender_mutex);if(sender.status.wire_mode==OtaEspNowWireMode::OfficialPackets)capacity=ESPNOW_OTA_PACKET_MAX_SIZE;}
  struct Read {uint32_t offset,total;size_t capacity;std::array<uint8_t,209> bytes{};size_t count=0;} context{offset,total,capacity};
  std::string error;
  const auto consume=[](size_t bytes,void *opaque,std::string &error){auto &read=*static_cast<Read*>(opaque);
    if(bytes!=read.total||read.offset>=read.total){error="espnow_ota_source_size_changed";return false;}
    read.count=std::min(read.capacity,size_t(read.total-read.offset));
    std::string path;{StaticLockGuard lock(sender_mutex);path=sender.path;}
    if(!storage_read_range(path.c_str(),read.offset,read.bytes.data(),read.count)){error="espnow_ota_source_read_failed";return false;}return true;};
  const bool ok=authorized(nullptr)&&!network_job_cancellation_requested(handle)&&storage_with_pinned_file(path.c_str(),consume,&context,error);
  {StaticLockGuard lock(sender_mutex);sender.read_ready=ok;sender.read_failed=!ok;
    if(ok){std::memcpy(sender.chunk.data(),context.bytes.data(),context.count);sender.chunk_size=context.count;}else sender.status.last_error=error.empty()?"espnow_ota_source_cancelled":error;}
  return {ok,false,error};
}
void fail(const char *why){sender.status.last_error=why;sender.status.phase=OtaEspNowSendPhase::Failed;sender.cancelled=true;}
}
OtaEspNowSendStatus ota_service_espnow_send_status(){StaticLockGuard lock(sender_mutex);return sender.status;}
bool ota_service_start_espnow_send(KernelRuntime &runtime,const OtaEspNowSendRequest &request,std::string &error){
  if(!internal::espnow_ota_admitted()){error="espnow_ota_service_stopped";return false;}
  digest::Sha256Value sha{};
  if(!request.source_path||strnlen(request.source_path,241)>240||!storage_is_safe_path(request.source_path)||
      request.source_path[0]!='/'||!request.package_sha256||strnlen(request.package_sha256,65)!=64||
      !digest::decode_hex(request.package_sha256,sha.data(),sha.size())||!request.package_size||request.package_size>CONFIG_ESPNOW_OTA_FIRMWARE_SIZE_MAX||
      request.duration_ms<100||request.duration_ms>900000||!request.authorized||!request.authorized(request.context)||
      (request.wire_mode!=OtaEspNowWireMode::LunarEnvelope&&request.wire_mode!=OtaEspNowWireMode::OfficialPackets)){
    error="espnow_ota_sender_invalid_request";return false;
  }
  {
    StaticLockGuard lock(sender_mutex);
    if(sender.status.active||sender.client_owned||sender.opening||network_job_active(sender.job)){error="espnow_ota_sender_busy";return false;}
    if(sender.status.generation==std::numeric_limits<uint32_t>::max()){error="espnow_ota_sender_generation_exhausted";return false;}
    if(!sender.chunk.resize(209)){error="espnow_ota_sender_chunk_allocation_failed";return false;}
    const auto generation=sender.status.generation+1;sender.status={};sender.status.generation=generation;
    sender.status.active=true;sender.status.phase=OtaEspNowSendPhase::Handshake;sender.status.expected_bytes=request.package_size;
    sender.status.wire_mode=request.wire_mode;
    sender.runtime=&runtime;sender.authorized=request.authorized;sender.context=request.context;sender.path=request.source_path;sender.sha=sha;
    sender.deadline=now()+request.duration_ms;sender.cancelled=false;sender.client_retired=false;sender.opening=true;
    sender.reply_ready=sender.reply_invalid=sender.read_ready=sender.read_failed=false;sender.outstanding=0;sender.next_poll=0;
    sender.progress_bytes=0;sender.progress_checked=false;
  }
  EspNowManagementClientOptions options;options.families=8;options.duration_ms=request.duration_ms;
  options.authorized=authorized;options.response=response;options.retired=retired;options.response_accepted=response_accepted;
  const auto previous=espnow_management_client_status();
  const EspNowManagementClientPeer peer{request.peer,request.proof_of_possession};
  const bool opened=espnow_open_management_client(runtime,peer,options,error);
  const auto client=espnow_management_client_status();
  {
    StaticLockGuard lock(sender_mutex);sender.opening=false;
    sender.client_owned=opened||(!sender.client_retired&&client.generation!=previous.generation&&client.retiring);
    sender.client_generation=client.generation;
    if(!opened){fail(error.c_str());if(!sender.client_owned){sender.status.active=false;sender.authorized=nullptr;sender.context=nullptr;sender.runtime=nullptr;sender.path.clear();sender.sha.fill(0);sender.chunk.clear();}}
  }
  return opened;
}
bool ota_service_cancel_espnow_send(std::string &error){
  StaticLockGuard lock(sender_mutex);
  if(sender.status.active){sender.cancelled=true;sender.status.phase=OtaEspNowSendPhase::Retiring;sender.status.last_error="espnow_ota_sender_cancelled";}
  error.clear();return true;
}
void internal::poll_espnow_ota_sender(){
  {StaticLockGuard lock(sender_mutex);if(!sender.status.active||sender.opening)return;}
  KernelRuntime *runtime=nullptr;bool close=false;
  const auto client=espnow_management_client_status();
  {
    StaticLockGuard lock(sender_mutex);if(!sender.status.active||sender.opening)return;
    runtime=sender.runtime;
    if(now()>=sender.deadline)fail("espnow_ota_sender_deadline");
    if(sender.reply_invalid)fail("espnow_ota_sender_invalid_reply");
    if(sender.read_failed)fail(sender.status.last_error.c_str());
    if(sender.status.phase==OtaEspNowSendPhase::Reading&&!network_job_active(sender.job)&&!sender.read_ready)fail("espnow_ota_sender_read_cancelled");
    if(sender.client_owned&&client.generation!=sender.client_generation)fail("espnow_ota_sender_client_replaced");
    if(sender.client_retired&&sender.status.phase!=OtaEspNowSendPhase::InstallRequested&&!sender.cancelled)fail("espnow_ota_sender_transport_closed");
    if(sender.status.phase==OtaEspNowSendPhase::InstallRequested||sender.cancelled){
      close=sender.client_owned&&!sender.client_retired;
      if(!close&&!network_job_active(sender.job)){
        sender.status.active=false;if(sender.status.phase==OtaEspNowSendPhase::Retiring)sender.status.phase=OtaEspNowSendPhase::Failed;
        sender.client_owned=false;sender.authorized=nullptr;sender.context=nullptr;sender.runtime=nullptr;
        sender.path.clear();sender.chunk.fill(0);sender.chunk.clear();sender.sha.fill(0);sender.reply.fill(0);return;
      }
    }
  }
  if(close){std::string error;(void)espnow_close_management_client(*runtime,error);return;}
  if(!authorized(nullptr)){StaticLockGuard lock(sender_mutex);fail("espnow_ota_sender_authority_expired");return;}
  std::array<uint8_t,218> command{};size_t length=0;uint8_t kind=0;
  {
    StaticLockGuard lock(sender_mutex);
    if(sender.cancelled||sender.status.phase==OtaEspNowSendPhase::InstallRequested)return;
    if(sender.reply_ready){
      sender.reply_ready=false;const auto &reply=sender.reply;
      const auto phase=OtaEspNowTransferPhase(reply[2]);const auto id=get32(reply.data()+4),total=get32(reply.data()+8),persisted=get32(reply.data()+16);
      if(total!=sender.status.expected_bytes||!id||(sender.status.transfer_id&&id!=sender.status.transfer_id)||persisted<sender.status.persisted_bytes||persisted>total){fail("espnow_ota_sender_progress_mismatch");return;}
      sender.status.transfer_id=id;sender.status.persisted_bytes=persisted;
      if(phase==OtaEspNowTransferPhase::Failed||phase==OtaEspNowTransferPhase::Retiring||phase==OtaEspNowTransferPhase::Idle){fail("espnow_ota_remote_failed");return;}
      if(sender.outstanding==6){sender.status.phase=OtaEspNowSendPhase::InstallRequested;sender.outstanding=0;return;}
      const auto replied=sender.outstanding;sender.outstanding=0;sender.next_poll=now();
      if(replied==10){sender.progress_bytes=persisted;sender.progress_checked=true;}
      if(phase==OtaEspNowTransferPhase::Receiving){
        if(sender.status.wire_mode==OtaEspNowWireMode::OfficialPackets&&replied!=10&&
            (!sender.progress_checked||persisted==total||persisted-sender.progress_bytes>=32*ESPNOW_OTA_PACKET_MAX_SIZE))
          sender.status.phase=OtaEspNowSendPhase::QueryProgress;
        else if(persisted==total)sender.status.phase=OtaEspNowSendPhase::Verify;
        else {
          if(network_job_active(sender.job)){sender.status.phase=OtaEspNowSendPhase::WaitingRemote;return;}
          NetworkJobDefinition job;job.owner_service_id=kUpdateServiceId;job.kind="espnow.source";job.priority=kNetworkPriorityControl;job.runner=read;
          std::string error;if(!submit_network_job(job,sender.job,error)){fail(error.c_str());return;}
          sender.read_ready=false;sender.status.phase=OtaEspNowSendPhase::Reading;
        }
      }else if(phase==OtaEspNowTransferPhase::Ready)sender.status.phase=OtaEspNowSendPhase::Install;
      else {sender.status.phase=OtaEspNowSendPhase::WaitingRemote;sender.next_poll=now()+10;}
    }
    if(client.request_pending||!client.authenticated||sender.outstanding||now()<sender.next_poll)return;
    switch(sender.status.phase){
      case OtaEspNowSendPhase::Handshake:
        kind=3;command[0]=kind;put32(command.data()+1,sender.status.expected_bytes);std::memcpy(command.data()+5,sender.sha.data(),32);length=37;sender.status.phase=OtaEspNowSendPhase::Begin;break;
      case OtaEspNowSendPhase::WaitingRemote:kind=8;command[0]=kind;length=1;break;
      case OtaEspNowSendPhase::QueryProgress:{
        const auto packets=(sender.status.expected_bytes+ESPNOW_OTA_PACKET_MAX_SIZE-1)/ESPNOW_OTA_PACKET_MAX_SIZE;
        sender.progress_page=uint8_t(std::min(packets-1,sender.status.persisted_bytes/ESPNOW_OTA_PACKET_MAX_SIZE)/(ESPNOW_OTA_PROGRESS_MAX_SIZE*8));
        kind=10;command[0]=kind;put32(command.data()+1,sender.status.transfer_id);command[5]=sender.progress_page;length=6;break;
      }
      case OtaEspNowSendPhase::Reading:
        if(!sender.read_ready||network_job_active(sender.job))return;
        if(sender.status.wire_mode==OtaEspNowWireMode::OfficialPackets){
          espnow_ota_packet_t packet{};packet.type=ESPNOW_OTA_TYPE_DATA;
          packet.seq=uint16_t(sender.status.persisted_bytes/ESPNOW_OTA_PACKET_MAX_SIZE);packet.size=uint8_t(sender.chunk_size);
          std::memcpy(packet.data,sender.chunk.data(),sender.chunk_size);
          kind=9;command[0]=kind;put32(command.data()+1,sender.status.transfer_id);
          length=5+offsetof(espnow_ota_packet_t,data)+sender.chunk_size;std::memcpy(command.data()+5,&packet,length-5);
        }else{
          kind=4;command[0]=kind;put32(command.data()+1,sender.status.transfer_id);put32(command.data()+5,sender.status.persisted_bytes);
          std::memcpy(command.data()+9,sender.chunk.data(),sender.chunk_size);length=sender.chunk_size+9;
        }
        sender.read_ready=false;sender.status.phase=OtaEspNowSendPhase::Sending;break;
      case OtaEspNowSendPhase::Verify:case OtaEspNowSendPhase::Install:
        kind=sender.status.phase==OtaEspNowSendPhase::Verify?5:6;command[0]=kind;put32(command.data()+1,sender.status.transfer_id);length=5;break;
      default:return;
    }
    sender.outstanding=kind;
  }
  std::string error;
  if(!espnow_management_client_request(*runtime,EspNowManagementFamily::Ota,command.data(),length,error)){
    StaticLockGuard lock(sender_mutex);fail(error.c_str());
  }
  command.fill(0);
}
bool internal::stop_espnow_ota_sender(std::string &error){
  (void)ota_service_cancel_espnow_send(error);poll_espnow_ota_sender();
  StaticLockGuard lock(sender_mutex);
  if(sender.status.active||sender.client_owned||network_job_active(sender.job)){error="espnow_ota_sender_retiring";return false;}
  error.clear();return true;
}
}
#else
namespace lunaredu::lunaros {
OtaEspNowSendStatus ota_service_espnow_send_status(){return {};}
bool ota_service_start_espnow_send(KernelRuntime &,const OtaEspNowSendRequest &,std::string &error){error="espnow_ota_storage_unavailable";return false;}
bool ota_service_cancel_espnow_send(std::string &error){error.clear();return true;}
void internal::poll_espnow_ota_sender(){}
bool internal::stop_espnow_ota_sender(std::string &error){error.clear();return true;}
}
#endif
