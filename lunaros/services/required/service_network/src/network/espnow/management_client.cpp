#include "lunaros_network_espnow_client.h"
#include "network/espnow/management_internal.h"
#include "network/espnow/group_internal.h"
#include "kernel/static_mutex.h"
#include "kernel/registration_storage.h"
#include "sdkconfig.h"
#if CONFIG_LUNAREDU_NETWORK_ENABLE_ESPNOW && CONFIG_LUNAROS_NETWORK_ESPNOW_MANAGEMENT
#include <esp_timer.h>
#endif

namespace lunaredu::lunaros {
namespace {
StaticMutex client_mutex;
EspNowManagementClientStatus client_published;
}
EspNowManagementClientStatus espnow_management_client_status(){
  StaticLockGuard lock(client_mutex);auto result=client_published;
#if CONFIG_LUNAREDU_NETWORK_ENABLE_ESPNOW && CONFIG_LUNAROS_NETWORK_ESPNOW_MANAGEMENT
  const auto current=uint64_t(esp_timer_get_time())/1000;
  if(result.active){
    if(current>=result.expires_at_ms){result.active=result.authenticated=false;result.retiring=true;result.remaining_ms=0;}
    else result.remaining_ms=uint32_t(result.expires_at_ms-current);
  }
#endif
  return result;
}
}

#if CONFIG_LUNAREDU_NETWORK_ENABLE_ESPNOW && CONFIG_LUNAROS_NETWORK_ESPNOW_MANAGEMENT
#include "lunaros_network_espnow.h"
#include "lunaros_network_espnow_management_wire.h"
#include "kernel/runtime.h"
#include "kernel/service_executor.h"
#include "network/network_service_internal.h"
#include <espnow.h>
#include <esp_now.h>
#include <esp_timer.h>
#include <esp_random.h>
#ifdef ESP_PLATFORM
#include <esp_attr.h>
#endif
#include <mbedtls/version.h>
#include <mbedtls/platform_util.h>
#include <protocomm_client_security1.h>
#include <espnow_security_handshake.h>
#include <lunar_espnow_owner.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

namespace lunaredu::lunaros {
namespace {
enum class Phase { Idle, Handshake0, Handshake1, KeyEcho, Welcome, Ready, Reply, Advance };
struct Peer {std::array<uint8_t,6> mac{};std::array<char,65> pop{};};
struct Client {
  RegistrationArray<Peer> peers{true};
  EspNowManagementClientOptions options;
  EspNowManagementClientStatus status;
  protocomm_security_handle_t security=nullptr;
  protocomm_security1_params_t pop{};
  std::array<uint8_t,32> key{};
  RegistrationArray<uint8_t> command{true};
  size_t command_size=0;
  EspNowManagementFamily family=EspNowManagementFamily::Provisioning;
  uint64_t deadline=0,response_deadline=0,next_welcome=0,nonce=0;
  Phase phase=Phase::Idle;
  bool peer_added=false,crypto_owned=false,batch=false;
  unsigned packet_budget=0;
};
#if defined(ESP_PLATFORM) && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
EXT_RAM_BSS_ATTR
#endif
Client client;
uint64_t now(){return uint64_t(esp_timer_get_time())/1000;}
void wipe(void *data,size_t size){mbedtls_platform_zeroize(data,size);}
void free_output(uint8_t *data,ssize_t size){if(data){if(size>0)wipe(data,size_t(size));std::free(data);}}
Peer &peer(){return client.peers[client.status.peer_index];}
void publish(){
  const auto current=now();client.status.remaining_ms=client.status.active&&current<client.deadline?uint32_t(client.deadline-current):0;
  client.status.expires_at_ms=client.deadline;
  StaticLockGuard lock(client_mutex);client_published=client.status;
}
bool authorized(){return client.status.active&&!client.status.retiring&&now()<client.deadline&&
  network_maintenance_state()==NetworkMaintenanceState::Normal&&client.options.authorized&&client.options.authorized(client.options.context);}
void fail(EspNowManagementClientResult result,const char *why){
  client.status.results[client.status.peer_index]=result;client.status.last_result=why;
  client.status.authenticated=false;client.status.request_pending=false;client.phase=Phase::Advance;publish();
}
esp_err_t send(espnow_data_type_t type,const void *data,size_t size,bool encrypted){
  if(!authorized())return ESP_ERR_INVALID_STATE;
  espnow_frame_head_t head{};head.retransmit_count=1;head.filter_adjacent_channel=true;head.security=encrypted;
  return ::espnow_send(type,peer().mac.data(),data,size,&head,0);
}
bool send_security(uint8_t type,const uint8_t *data,size_t size){
  if(!data||!size||size>248)return false;
  std::array<uint8_t,250> packet{};packet[0]=type;packet[1]=uint8_t(size);std::memcpy(packet.data()+2,data,size);
  const auto result=send(ESPNOW_DATA_TYPE_SECURITY,packet.data(),size+2,false);wipe(packet.data(),packet.size());
  return result==ESP_OK;
}
bool retire_peer(std::string &error){
  client.status.authenticated=false;client.status.request_pending=false;
  if(client.crypto_owned){
    if(lunar_espnow_management_close()!=ESP_OK){error="espnow_client_crypto_retiring";return false;}
    client.crypto_owned=false;
  }
  if(client.security){
    if(protocomm_client_security1.cleanup(client.security)!=ESP_OK){error="espnow_client_security_retiring";return false;}
    client.security=nullptr;
  }
  if(client.peer_added){
    const auto result=esp_now_del_peer(peer().mac.data());
    if(result!=ESP_OK&&result!=ESP_ERR_ESPNOW_NOT_FOUND&&result!=ESP_ERR_ESPNOW_NOT_INIT){error="espnow_client_peer_retiring";return false;}
    client.peer_added=false;
  }
  wipe(client.key.data(),client.key.size());client.pop={};client.nonce=0;
  error.clear();return true;
}
bool finish(std::string &error){
  for(size_t i=0;i<client.status.peer_count;++i)if(client.status.results[i]==EspNowManagementClientResult::Pending)client.status.results[i]=EspNowManagementClientResult::Cancelled;
  client.status.active=false;client.status.retiring=true;publish();
  if(!retire_peer(error)){client.status.last_result=error;publish();return false;}
  const auto retired=client.options.retired;void *context=client.options.context;
  wipe(client.peers.data(),client.peers.size()*sizeof(Peer));client.peers.clear();
  if(client.command.size())wipe(client.command.data(),client.command.size());
  client.command.clear();
  client.options={};client.phase=Phase::Idle;client.command_size=0;client.status.retiring=false;
  publish();if(retired)retired(context);error.clear();return true;
}
bool same_source(const uint8_t *source){return source&&std::memcmp(source,peer().mac.data(),6)==0;}
esp_err_t security_packet(uint8_t *source,void *data,size_t size,wifi_pkt_rx_ctrl_t*){
  if(!authorized()||!same_source(source)||!data||size<2||size>250||!client.packet_budget)return ESP_ERR_INVALID_STATE;
  --client.packet_budget;const auto *bytes=static_cast<const uint8_t*>(data);
  if(bytes[1]!=size-2||!bytes[1])return ESP_ERR_INVALID_SIZE;
  const auto &security=protocomm_client_security1;
  uint8_t *output=nullptr;ssize_t length=0;esp_err_t result=ESP_ERR_INVALID_STATE;
  if(bytes[0]==ESPNOW_SEC_TYPE_HANDSHAKE&&(client.phase==Phase::Handshake0||client.phase==Phase::Handshake1)){
    const auto previous=client.phase;
    result=security.security_req_handler(client.security,&client.pop,client.status.generation,bytes+2,ssize_t(size-2),&output,&length,nullptr);
    if(result==ESP_OK&&previous==Phase::Handshake0){
      if(length<=0||!send_security(ESPNOW_SEC_TYPE_HANDSHAKE,output,size_t(length)))result=ESP_FAIL;
      else client.phase=Phase::Handshake1;
    }else if(result==ESP_OK){
      if(length!=0)result=ESP_ERR_INVALID_SIZE;
      free_output(output,length);output=nullptr;length=0;
      if(result==ESP_OK){
        esp_fill_random(client.key.data(),client.key.size());
        result=security.encrypt(client.security,client.status.generation,client.key.data(),client.key.size(),&output,&length);
        if(result==ESP_OK&&(length!=APP_KEY_LEN||!send_security(ESPNOW_SEC_TYPE_KEY,output,size_t(length))))result=ESP_FAIL;
        if(result==ESP_OK)client.phase=Phase::KeyEcho;
      }
    }
  }else if(bytes[0]==ESPNOW_SEC_TYPE_KEY_RESP&&client.phase==Phase::KeyEcho){
    result=security.decrypt(client.security,client.status.generation,bytes+2,ssize_t(size-2),&output,&length);
    uint8_t different=0;
    if(result==ESP_OK&&length==APP_KEY_LEN&&output){for(size_t i=0;i<client.key.size();++i)different|=output[i]^client.key[i];}
    else different=1;
    if(different)result=ESP_ERR_INVALID_ARG;
    if(result==ESP_OK)result=lunar_espnow_management_set_key(client.key.data());
    if(result==ESP_OK){client.phase=Phase::Welcome;client.next_welcome=now();}
  }else return ESP_ERR_INVALID_STATE;
  free_output(output,length);
  if(result!=ESP_OK)fail(EspNowManagementClientResult::TransportFailed,"espnow_client_handshake_failed");
  else client.response_deadline=now()+client.options.response_timeout_ms;
  publish();return result;
}
esp_err_t welcome(uint8_t *source,void *data,size_t size,wifi_pkt_rx_ctrl_t*){
  if(!authorized()||!same_source(source)||client.phase!=Phase::Welcome||!client.packet_budget)return ESP_ERR_INVALID_STATE;
  --client.packet_budget;EspNowManagementEnvelope envelope;
  if(!espnow_decode_management(static_cast<const uint8_t*>(data),size,envelope)||envelope.family||envelope.flags!=2||!envelope.nonce||envelope.sequence||envelope.size)return ESP_ERR_INVALID_ARG;
  client.nonce=envelope.nonce;client.status.authenticated=true;client.phase=Phase::Ready;
  wipe(client.key.data(),client.key.size());publish();return ESP_OK;
}
esp_err_t response(EspNowManagementFamily family,uint8_t *source,void *data,size_t size){
  if(!authorized()||!same_source(source)||client.phase!=Phase::Reply||client.family!=family||!client.packet_budget)return ESP_ERR_INVALID_STATE;
  --client.packet_budget;EspNowManagementEnvelope envelope;
  if(!espnow_decode_management(static_cast<const uint8_t*>(data),size,envelope)||envelope.flags!=1||envelope.family!=unsigned(family)||
    envelope.nonce!=client.nonce||envelope.sequence!=client.status.sequence||!envelope.size)return ESP_ERR_INVALID_ARG;
  client.status.request_pending=false;client.phase=Phase::Ready;
  const bool accepted=client.options.response_accepted?
    client.options.response_accepted(client.options.context,family,envelope.payload,envelope.size):envelope.payload[0]==0;
  client.status.results[client.status.peer_index]=accepted?EspNowManagementClientResult::Succeeded:EspNowManagementClientResult::Rejected;
  client.status.last_result=accepted?"espnow_client_request_completed":"espnow_client_request_rejected";
  if(client.options.response)client.options.response(client.options.context,client.status.peer_index,family,envelope.sequence,envelope.payload,envelope.size);
  if(client.batch)client.phase=Phase::Advance;
  publish();return ESP_OK;
}
esp_err_t prov(uint8_t *s,void *d,size_t n,wifi_pkt_rx_ctrl_t*){return response(EspNowManagementFamily::Provisioning,s,d,n);}
esp_err_t diag(uint8_t *s,void *d,size_t n,wifi_pkt_rx_ctrl_t*){return response(EspNowManagementFamily::Diagnostics,s,d,n);}
esp_err_t ota(uint8_t *s,void *d,size_t n,wifi_pkt_rx_ctrl_t*){return response(EspNowManagementFamily::Ota,s,d,n);}
bool begin_peer(std::string &error){
  client.status.sequence=0;client.status.authenticated=false;client.phase=Phase::Handshake0;
  auto failed=[&](const char *reason){error=reason;fail(EspNowManagementClientResult::TransportFailed,reason);return false;};
  if(!esp_now_is_peer_exist(peer().mac.data())){
    esp_now_peer_info_t info{};std::memcpy(info.peer_addr,peer().mac.data(),6);info.ifidx=WIFI_IF_STA;
    if(esp_now_add_peer(&info)!=ESP_OK)return failed("espnow_client_peer_failed");
    client.peer_added=true;
  }
  // Even a partially allocated crypto context is ours until close succeeds.
  client.crypto_owned=true;
  if(lunar_espnow_management_open_client(peer().mac.data())!=ESP_OK)return failed("espnow_client_crypto_failed");
  const auto &security=protocomm_client_security1;
  client.pop={reinterpret_cast<const uint8_t*>(peer().pop.data()),uint16_t(std::strlen(peer().pop.data()))};
  if(security.init(&client.security)!=ESP_OK||security.new_transport_session(client.security,client.status.generation)!=ESP_OK)return failed("espnow_client_security_failed");
  for(const auto &entry:{std::pair{ESPNOW_DATA_TYPE_SECURITY,&security_packet},std::pair{ESPNOW_DATA_TYPE_SECURITY_DATA,&welcome},
      std::pair{ESPNOW_DATA_TYPE_PROV,&prov},std::pair{ESPNOW_DATA_TYPE_DEBUG_LOG,&diag},std::pair{ESPNOW_DATA_TYPE_OTA_STATUS,&ota}}){
    if(espnow_set_config_for_data_type(entry.first,true,entry.second)!=ESP_OK)return failed("espnow_client_transport_failed");
  }
  uint8_t *output=nullptr;ssize_t length=0;const auto result=write_security1_command0(&output,&length);
  const bool sent=result==ESP_OK&&length>0&&send_security(ESPNOW_SEC_TYPE_HANDSHAKE,output,size_t(length));free_output(output,length);
  if(!sent)return failed("espnow_client_command0_failed");
  client.response_deadline=now()+client.options.response_timeout_ms;publish();error.clear();return true;
}
bool request_on_owner(EspNowManagementFamily family,const uint8_t *data,size_t size,std::string &error){
  const auto index=unsigned(family);
  if(!authorized()||client.phase!=Phase::Ready||index<1||index>3||!(client.options.families&(1u<<index))||!data||!size||size>kEspNowManagementPayloadBytes){error="espnow_client_request_not_admitted";return false;}
  if(client.status.sequence==std::numeric_limits<uint32_t>::max()){error="espnow_client_sequence_exhausted";return false;}
  client.family=family;++client.status.sequence;
  std::array<uint8_t,238> packet{};size_t length=packet.size();
  const EspNowManagementEnvelope envelope{uint8_t(index),0,client.nonce,client.status.sequence,data,size};
  if(!espnow_encode_management(envelope,packet.data(),length)){error="espnow_client_encode_failed";return false;}
  const auto type=family==EspNowManagementFamily::Provisioning?ESPNOW_DATA_TYPE_PROV:family==EspNowManagementFamily::Diagnostics?ESPNOW_DATA_TYPE_DEBUG_COMMAND:ESPNOW_DATA_TYPE_OTA_DATA;
  const auto result=send(type,packet.data(),length,true);wipe(packet.data(),packet.size());
  if(result!=ESP_OK){error="espnow_client_send_failed";fail(EspNowManagementClientResult::TransportFailed,error.c_str());return false;}
  client.status.request_pending=true;client.phase=Phase::Reply;client.response_deadline=now()+client.options.response_timeout_ms;
  publish();error.clear();return true;
}
struct Control {
  KernelRuntime *runtime;const EspNowManagementClientPeer *peers=nullptr;size_t count=0;
  const EspNowManagementClientOptions *options=nullptr;EspNowManagementFamily family{};
  const uint8_t *data=nullptr;size_t size=0;bool close=false,batch=false;
};
void execute(void *opaque,ServiceOperationResult &result){
  const auto &request=*static_cast<Control*>(opaque);
  if(!require_network_runtime(*request.runtime,result.error)){result.ok=false;return;}
  if(request.close){result.ok=internal::stop_espnow_client(result.error);return;}
  if(!admit_network_work(result.error)){result.ok=false;return;}
  if(!request.options){
    if(client.batch){result.error="espnow_batch_owns_request";result.ok=false;return;}
    result.ok=request_on_owner(request.family,request.data,request.size,result.error);return;
  }
  const auto &options=*request.options;
  if(!espnow_ready()||internal::espnow_responder_busy()||internal::espnow_client_busy()||internal::espnow_group_busy()||network_maintenance_state()!=NetworkMaintenanceState::Normal){result.error="espnow_client_busy";result.ok=false;return;}
  if(!request.peers||!request.count||request.count>kEspNowManagementBatchPeers||options.duration_ms<100||options.duration_ms>900000||
      options.response_timeout_ms<100||options.response_timeout_ms>10000||!options.families||(options.families&~uint8_t(14))||
      !options.authorized||!options.authorized(options.context)) {result.error="espnow_client_invalid_authority";result.ok=false;return;}
  if(request.batch&&(unsigned(request.family)<1||unsigned(request.family)>3||!(options.families&(1u<<unsigned(request.family)))||
      !request.data||!request.size||request.size>kEspNowManagementPayloadBytes)){result.error="espnow_batch_invalid_command";result.ok=false;return;}
  for(size_t i=0;i<request.count;++i){
    const auto &item=request.peers[i];bool empty=true;for(auto byte:item.mac)empty&=byte==0;
    const auto length=item.proof_of_possession?strnlen(item.proof_of_possession,65):0;
    if(empty||(item.mac[0]&1)||length<8||length>64){result.error="espnow_client_invalid_peer";result.ok=false;return;}
    for(size_t j=0;j<i;++j)if(item.mac==request.peers[j].mac){result.error="espnow_batch_duplicate_peer";result.ok=false;return;}
  }
  if(client.status.generation==std::numeric_limits<uint32_t>::max()){result.error="espnow_client_generation_exhausted";result.ok=false;return;}
  if(!client.peers.resize(request.count)){result.error="espnow_client_peer_allocation_failed";result.ok=false;return;}
  if(request.batch&&!client.command.resize(request.size)){client.peers.clear();result.error="espnow_client_command_allocation_failed";result.ok=false;return;}
  const auto generation=client.status.generation+1;client.status={};client.status.generation=generation;
  client.options=options;client.batch=request.batch;client.status.peer_count=uint8_t(request.count);
  for(size_t i=0;i<request.count;++i){client.peers[i].mac=request.peers[i].mac;std::strcpy(client.peers[i].pop.data(),request.peers[i].proof_of_possession);}
  client.command_size=request.size;client.family=request.family;
  if(request.batch)std::memcpy(client.command.data(),request.data,request.size);
  client.deadline=now()+options.duration_ms;client.status.active=true;
  result.ok=begin_peer(result.error);
  if(!result.ok){std::string cleanup;(void)finish(cleanup);}
}
bool invoke(KernelRuntime &runtime,Control &request,std::string &error){return runtime.invoke_executor(kNetworkServiceExecutor,execute,&request,error);}
}
bool espnow_open_management_client(KernelRuntime &runtime,const EspNowManagementClientPeer &peer,const EspNowManagementClientOptions &options,std::string &error){
  Control request{&runtime,&peer,1,&options};return invoke(runtime,request,error);
}
bool espnow_start_management_batch(KernelRuntime &runtime,const EspNowManagementClientPeer *peers,size_t count,const EspNowManagementClientOptions &options,
    EspNowManagementFamily family,const uint8_t *data,size_t size,std::string &error){
  Control request{&runtime,peers,count,&options,family,data,size,false,true};return invoke(runtime,request,error);
}
bool espnow_management_client_request(KernelRuntime &runtime,EspNowManagementFamily family,const uint8_t *data,size_t size,std::string &error){
  Control request{&runtime,nullptr,0,nullptr,family,data,size};return invoke(runtime,request,error);
}
bool espnow_close_management_client(KernelRuntime &runtime,std::string &error){Control request{&runtime};request.close=true;return invoke(runtime,request,error);}
bool internal::espnow_client_busy(){return client.status.active||client.status.retiring||client.security||client.crypto_owned||client.peer_added;}
bool internal::stop_espnow_client(std::string &error){
  if(!espnow_client_busy()){error.clear();return true;}
  for(size_t i=client.status.peer_index;i<client.status.peer_count;++i)if(client.status.results[i]==EspNowManagementClientResult::Pending)client.status.results[i]=EspNowManagementClientResult::Cancelled;
  return finish(error);
}
void internal::poll_espnow_client(){
  client.packet_budget=1;
  if(client.status.retiring){std::string error;(void)finish(error);return;}
  if(!client.status.active)return;
  if(!authorized()){std::string error;(void)stop_espnow_client(error);return;}
  if(client.phase==Phase::Advance){
    std::string error;
    if(!retire_peer(error)){client.status.last_result=error;publish();return;}
    if(!client.batch||client.status.peer_index+1>=client.status.peer_count){(void)finish(error);return;}
    wipe(peer().pop.data(),peer().pop.size());++client.status.peer_index;(void)begin_peer(error);return;
  }
  if(client.phase!=Phase::Ready&&now()>=client.response_deadline){fail(EspNowManagementClientResult::TimedOut,"espnow_client_response_timeout");return;}
  if(client.phase==Phase::Welcome&&now()>=client.next_welcome){const uint8_t request=ESPNOW_SEC_TYPE_REQUEST;(void)send(ESPNOW_DATA_TYPE_SECURITY,&request,1,false);client.next_welcome=now()+250;}
  if(client.batch&&client.phase==Phase::Ready){std::string error;(void)request_on_owner(client.family,client.command.data(),client.command_size,error);}
  publish();
}
}
#else
namespace lunaredu::lunaros {
bool espnow_open_management_client(KernelRuntime &,const EspNowManagementClientPeer &,const EspNowManagementClientOptions &,std::string &error){error="espnow_client_disabled_in_build";return false;}
bool espnow_start_management_batch(KernelRuntime &,const EspNowManagementClientPeer *,size_t,const EspNowManagementClientOptions &,EspNowManagementFamily,const uint8_t *,size_t,std::string &error){error="espnow_client_disabled_in_build";return false;}
bool espnow_management_client_request(KernelRuntime &,EspNowManagementFamily,const uint8_t *,size_t,std::string &error){error="espnow_client_disabled_in_build";return false;}
bool espnow_close_management_client(KernelRuntime &,std::string &error){error.clear();return true;}
bool internal::espnow_client_busy(){return false;}
bool internal::stop_espnow_client(std::string &error){error.clear();return true;}
void internal::poll_espnow_client(){}
}
#endif
