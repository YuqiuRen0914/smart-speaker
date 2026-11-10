#include "lunaros_network_espnow_management.h"
#include "lunaros_network_espnow_management_wire.h"
#include "network/espnow/management_internal.h"
#include "network/espnow/group_internal.h"
#include "kernel/static_mutex.h"
#include "sdkconfig.h"
#include <cstring>
#if CONFIG_LUNAREDU_NETWORK_ENABLE_ESPNOW && CONFIG_LUNAROS_NETWORK_ESPNOW_MANAGEMENT
#include <esp_timer.h>
#endif

namespace lunaredu::lunaros {
namespace {
StaticMutex management_control;
std::array<EspNowManagementHandler,4> providers{};
EspNowManagementStatus published;
}
bool espnow_bind_management_provider(EspNowManagementFamily family,EspNowManagementHandler handler){
  const auto index=unsigned(family);if(!handler||index<1||index>3)return false;
  StaticLockGuard lock(management_control);
  if(providers[index]==handler)return true;
  if(published.active||published.retiring||providers[index])return false;
  providers[index]=handler;return true;
}
EspNowManagementStatus espnow_management_status(){
  StaticLockGuard lock(management_control);auto result=published;
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
#include "kernel/runtime.h"
#include "kernel/service_executor.h"
#include "network/network_service_internal.h"
#include <espnow.h>
#include <esp_now.h>
#include <espnow_security_handshake.h>
#include <lunar_espnow_owner.h>
#include <mbedtls/platform_util.h>
#include <protocomm.h>
#include <protocomm_security1.h>
#include <esp_random.h>
#ifdef ESP_PLATFORM
#include <esp_attr.h>
#endif
#include <esp_timer.h>
#include <cstdlib>
#include <limits>
#include <utility>

namespace lunaredu::lunaros {
namespace {
struct Session {
  protocomm_t *pc=nullptr;
  EspNowManagementWindow window;
  std::array<uint8_t,65> pop{};
  protocomm_security1_params_t pop_params{};
  EspNowManagementStatus status;
  uint64_t deadline=0,nonce=0;
  uint32_t received_sequence=0;
  bool opened=false,key_pending=false,peer_added=false;
  unsigned packet_budget=0;
};
#if defined(ESP_PLATFORM) && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
EXT_RAM_BSS_ATTR
#endif
Session session;
uint64_t now(){return uint64_t(esp_timer_get_time())/1000;}
void publish(){
  const auto current=now();
  session.status.expires_at_ms=session.deadline;
  session.status.remaining_ms=session.status.active&&current<session.deadline?uint32_t(session.deadline-current):0;
  StaticLockGuard lock(management_control);published=session.status;
}
bool authorized(){return session.status.active&&!session.status.retiring&&now()<session.deadline&&
  network_maintenance_state()==NetworkMaintenanceState::Normal&&
  session.window.authorized&&session.window.authorized(session.window.context);}
void wipe(void *data,size_t size){mbedtls_platform_zeroize(data,size);}
esp_err_t send(espnow_data_type_t type,const void *data,size_t size,bool encrypted){
  if(!authorized())return ESP_ERR_INVALID_STATE;
  espnow_frame_head_t head{};head.retransmit_count=1;head.filter_adjacent_channel=true;head.security=encrypted;
  return ::espnow_send(type,session.window.peer.data(),data,size,&head,0);
}
esp_err_t reply(espnow_data_type_t type,uint8_t family,uint8_t flags,uint32_t sequence,const uint8_t *data,size_t size){
  std::array<uint8_t,238> bytes{};size_t length=bytes.size();
  const EspNowManagementEnvelope envelope{family,flags,session.nonce,sequence,data,size};
  if(!espnow_encode_management(envelope,bytes.data(),length))return ESP_ERR_INVALID_SIZE;
  const auto result=send(type,bytes.data(),length,true);wipe(bytes.data(),bytes.size());return result;
}
esp_err_t receive_key(uint32_t,const uint8_t *input,ssize_t length,uint8_t **output,ssize_t *output_length,void*){
  if(!authorized()||session.status.authenticated||session.key_pending||length!=APP_KEY_LEN)return ESP_ERR_INVALID_STATE;
  *output=static_cast<uint8_t*>(std::malloc(APP_KEY_LEN));if(!*output)return ESP_ERR_NO_MEM;
  // Protocomm reaches this endpoint only after PoP authentication/decryption.
  // Never install keys or expose a management endpoint from plaintext packets.
  auto result=lunar_espnow_management_set_key(input);
  if(result!=ESP_OK){std::free(*output);*output=nullptr;return result;}
  std::memcpy(*output,input,APP_KEY_LEN);*output_length=APP_KEY_LEN;session.key_pending=true;return ESP_OK;
}
esp_err_t handshake(uint8_t *source,void *data,size_t size,wifi_pkt_rx_ctrl_t*){
  if(!authorized()||!source||std::memcmp(source,session.window.peer.data(),6)||!data||!size||!session.packet_budget)return ESP_ERR_INVALID_STATE;
  --session.packet_budget;
  const auto *bytes=static_cast<const uint8_t*>(data);esp_err_t result=ESP_ERR_INVALID_ARG;
  if(bytes[0]==ESPNOW_SEC_TYPE_REQUEST&&size==1){
    espnow_sec_info_t info{};info.type=ESPNOW_SEC_TYPE_INFO;
    info.sec_ver=session.status.authenticated?ESPNOW_SEC_VER_V1_0:ESPNOW_SEC_VER_NONE;
    std::memcpy(info.client_mac,source,6);
    const auto sent=send(ESPNOW_DATA_TYPE_SECURITY_STATUS,&info,sizeof(info),false);
    if(sent!=ESP_OK||!session.status.authenticated)return sent;
    return reply(ESPNOW_DATA_TYPE_SECURITY_DATA,0,2,0,nullptr,0);
  }
  if(size<2||size>250||bytes[1]!=size-2||!bytes[1]||session.status.authenticated)return ESP_ERR_INVALID_ARG;
  const bool key=bytes[0]==ESPNOW_SEC_TYPE_KEY;
  if(!key&&bytes[0]!=ESPNOW_SEC_TYPE_HANDSHAKE)return ESP_ERR_INVALID_ARG;
  if(!session.opened){
    result=protocomm_open_session(session.pc,session.status.generation);
    if(result!=ESP_OK)return result;
    session.opened=true;
  }
  uint8_t *output=nullptr;ssize_t length=0;
  result=protocomm_req_handle(session.pc,key?"espnow-config":"espnow-session",session.status.generation,
      bytes+2,ssize_t(size-2),&output,&length);
  if(result==ESP_OK&&(length<=0||length>248||!output))result=ESP_ERR_INVALID_SIZE;
  if(result==ESP_OK){
    std::array<uint8_t,250> response{};response[0]=key?ESPNOW_SEC_TYPE_KEY_RESP:ESPNOW_SEC_TYPE_HANDSHAKE;
    response[1]=uint8_t(length);std::memcpy(response.data()+2,output,size_t(length));
    result=send(ESPNOW_DATA_TYPE_SECURITY,response.data(),size_t(length)+2,false);wipe(response.data(),response.size());
  }
  if(output){if(length>0)wipe(output,size_t(length));std::free(output);}
  if(result==ESP_OK&&key&&session.key_pending){
    session.status.authenticated=true;session.key_pending=false;
    // The initiator already received the key echo. A missing welcome can be
    // retried through REQUEST without reusing a CTR handshake/key packet.
    if(reply(ESPNOW_DATA_TYPE_SECURITY_DATA,0,2,0,nullptr,0)!=ESP_OK){
      session.status.last_result="security_welcome_retry_required";publish();return ESP_OK;
    }
  }
  session.status.last_result=result==ESP_OK?"security_packet_accepted":"security_handshake_failed";
  // A failed exchange is terminal. Reopening requires fresh native authority,
  // fresh nonce, fresh PoP handshake, and complete previous key retirement.
  if(result!=ESP_OK){session.status.active=false;session.status.retiring=true;}
  publish();return result;
}
esp_err_t receive(EspNowManagementFamily family,espnow_data_type_t response_type,uint8_t *source,void *data,size_t size){
  if(!authorized()||!session.status.authenticated||!source||std::memcmp(source,session.window.peer.data(),6)||!session.packet_budget)return ESP_ERR_INVALID_STATE;
  --session.packet_budget;EspNowManagementEnvelope envelope;
  const auto reject=[&](){++session.status.rejected;publish();return ESP_ERR_INVALID_ARG;};
  const auto index=unsigned(family);
  if(!(session.window.families&(1u<<index))||!espnow_decode_management(static_cast<const uint8_t*>(data),size,envelope)||
      envelope.flags||envelope.family!=index||envelope.nonce!=session.nonce||!envelope.sequence||envelope.sequence<=session.received_sequence)return reject();
  // Consume before dispatch: a failed/lost response must never repeat a write.
  session.received_sequence=envelope.sequence;
  EspNowManagementHandler provider=nullptr;{StaticLockGuard lock(management_control);provider=providers[index];}
  if(!provider)return reject();
  std::array<uint8_t,kEspNowManagementPayloadBytes> output{};size_t length=output.size();std::string error;
  const bool ok=provider({family,envelope.payload,envelope.size,envelope.sequence},output.data(),length,error);
  if(!ok||length>output.size()){
    ++session.status.rejected;session.status.last_result=ok?"management_reply_overflow":"management_request_rejected";
    output[0]=1;length=1;
  }else{++session.status.accepted;session.status.last_result="management_request_accepted";}
  const auto result=reply(response_type,uint8_t(index),1,envelope.sequence,output.data(),length);
  wipe(output.data(),output.size());publish();return result;
}
esp_err_t provisioning(uint8_t *s,void *d,size_t n,wifi_pkt_rx_ctrl_t*){return receive(EspNowManagementFamily::Provisioning,ESPNOW_DATA_TYPE_PROV,s,d,n);}
esp_err_t diagnostics(uint8_t *s,void *d,size_t n,wifi_pkt_rx_ctrl_t*){return receive(EspNowManagementFamily::Diagnostics,ESPNOW_DATA_TYPE_DEBUG_LOG,s,d,n);}
esp_err_t ota(uint8_t *s,void *d,size_t n,wifi_pkt_rx_ctrl_t*){return receive(EspNowManagementFamily::Ota,ESPNOW_DATA_TYPE_OTA_STATUS,s,d,n);}
bool open_on_owner(const EspNowManagementWindow &window,std::string &error){
  if(!espnow_ready()||session.pc||session.status.retiring||internal::espnow_client_busy()||internal::espnow_group_busy()||network_maintenance_state()!=NetworkMaintenanceState::Normal){error="espnow_management_not_ready";return false;}
  if((window.families&2)&&!internal::bind_espnow_provisioning()){error="espnow_provisioning_provider_conflict";return false;}
  const size_t pop_size=window.proof_of_possession?strnlen(window.proof_of_possession,65):0;
  bool empty=true;for(auto byte:window.peer)empty&=byte==0;
  const uint32_t maximum_duration=window.families==8?900000:60000;
  if(empty||(window.peer[0]&1)||pop_size<8||pop_size>64||window.duration_ms<100||window.duration_ms>maximum_duration||
      !window.families||(window.families&~uint8_t(14))||!window.authorized||!window.authorized(window.context)){
    error="espnow_management_invalid_authority";return false;
  }
  {StaticLockGuard lock(management_control);for(unsigned i=1;i<=3;++i)if((window.families&(1u<<i))&&!providers[i]){error="espnow_management_provider_missing";return false;}}
  if(session.status.generation==std::numeric_limits<uint32_t>::max()){error="espnow_management_generation_exhausted";return false;}
  ++session.status.generation;session.status.accepted=session.status.rejected=0;
  session.status.authenticated=false;session.status.families=window.families;session.status.last_result.clear();
  session.window=window;session.window.proof_of_possession=nullptr;
  std::memcpy(session.pop.data(),window.proof_of_possession,pop_size);
  session.pop_params={session.pop.data(),uint16_t(pop_size)};
  session.deadline=now()+window.duration_ms;esp_fill_random(&session.nonce,sizeof(session.nonce));
  if(!session.nonce)session.nonce=1;
  session.received_sequence=0;session.key_pending=false;
  session.status.active=true;session.status.retiring=false;
  auto fail=[&](const char *why){error=why;std::string cleanup;(void)internal::stop_espnow_management(cleanup);return false;};
  // Never alter/delete an existing product peer. The temporary management peer
  // follows the radio selected by Network and is removed only if we added it.
  if(!esp_now_is_peer_exist(window.peer.data())){
    esp_now_peer_info_t peer{};std::memcpy(peer.peer_addr,window.peer.data(),6);peer.ifidx=WIFI_IF_STA;
    if(esp_now_add_peer(&peer)!=ESP_OK)return fail("espnow_management_peer_failed");
    session.peer_added=true;
  }
  if(lunar_espnow_management_open(window.peer.data())!=ESP_OK)return fail("espnow_management_crypto_failed");
  session.pc=protocomm_new();if(!session.pc)return fail("espnow_management_no_memory");
  if(protocomm_set_security(session.pc,"espnow-session",&protocomm_security1,&session.pop_params)!=ESP_OK||
      protocomm_add_endpoint(session.pc,"espnow-config",receive_key,nullptr)!=ESP_OK)return fail("espnow_management_endpoint_failed");
  for(const auto &entry:{std::pair{ESPNOW_DATA_TYPE_SECURITY,&handshake},std::pair{ESPNOW_DATA_TYPE_PROV,&provisioning},
      std::pair{ESPNOW_DATA_TYPE_DEBUG_COMMAND,&diagnostics},std::pair{ESPNOW_DATA_TYPE_OTA_DATA,&ota}}){
    if(espnow_set_config_for_data_type(entry.first,true,entry.second)!=ESP_OK)return fail("espnow_management_transport_failed");
  }
  publish();error.clear();return true;
}
struct Request {KernelRuntime *runtime;const EspNowManagementWindow *window;};
void execute(void *opaque,ServiceOperationResult &result){
  const auto &request=*static_cast<Request*>(opaque);
  if(!require_network_runtime(*request.runtime,result.error)){result.ok=false;return;}
  if(!request.window){
    if(internal::espnow_client_busy()){result.error="espnow_management_owned_by_client";result.ok=false;return;}
    result.ok=internal::stop_espnow_management(result.error);return;
  }
  if(!admit_network_work(result.error)){result.ok=false;return;}
  result.ok=open_on_owner(*request.window,result.error);
}
}
bool espnow_open_management(KernelRuntime &runtime,const EspNowManagementWindow &window,std::string &error){
  Request request{&runtime,&window};return runtime.invoke_executor(kNetworkServiceExecutor,execute,&request,error);
}
bool espnow_close_management(KernelRuntime &runtime,std::string &error){
  Request request{&runtime,nullptr};return runtime.invoke_executor(kNetworkServiceExecutor,execute,&request,error);
}
bool internal::stop_espnow_management(std::string &error){
  session.status.active=false;session.status.authenticated=false;session.status.retiring=true;publish();
  if(lunar_espnow_management_close()!=ESP_OK){error="espnow_management_crypto_retiring";return false;}
  if(session.pc&&session.opened){
    if(protocomm_close_session(session.pc,session.status.generation)!=ESP_OK){error="espnow_management_session_retiring";return false;}
    session.opened=false;
  }
  if(session.pc){protocomm_delete(session.pc);session.pc=nullptr;}
  if(session.peer_added){
    const auto result=esp_now_del_peer(session.window.peer.data());
    if(result!=ESP_OK&&result!=ESP_ERR_ESPNOW_NOT_FOUND&&result!=ESP_ERR_ESPNOW_NOT_INIT){error="espnow_management_peer_retiring";return false;}
    session.peer_added=false;
  }
  const auto retired=session.window.retired;void *context=session.window.context;
  wipe(session.pop.data(),session.pop.size());session.pop_params={};session.window={};session.nonce=0;session.key_pending=false;
  if(retired)retired(context);
  session.status.retiring=false;session.status.families=0;session.status.remaining_ms=0;
  publish();error.clear();return true;
}
void internal::poll_espnow_management(){
  if(session.status.retiring||(session.status.active&&!authorized())){std::string error;(void)stop_espnow_management(error);}
  session.packet_budget=1;publish();
}
bool internal::espnow_responder_busy(){return session.pc||session.peer_added||session.status.active||session.status.retiring;}
}
#else
namespace lunaredu::lunaros {
bool espnow_open_management(KernelRuntime &,const EspNowManagementWindow &,std::string &error){error="espnow_management_disabled_in_build";return false;}
bool espnow_close_management(KernelRuntime &,std::string &error){error.clear();return true;}
bool internal::stop_espnow_management(std::string &error){error.clear();return true;}
void internal::poll_espnow_management(){}
bool internal::espnow_responder_busy(){return false;}
}
#endif
