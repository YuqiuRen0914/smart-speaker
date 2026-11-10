#include "sdkconfig.h"
#include "lunaros_network_espnow_group.h"
#include "group_internal.h"
#include "kernel/static_mutex.h"
#include "kernel/registration_storage.h"
#include <algorithm>
#include <cstring>
#if CONFIG_LUNAREDU_NETWORK_ENABLE_ESPNOW && CONFIG_LUNAROS_NETWORK_ESPNOW_MANAGEMENT
#include "lunaros_network_espnow.h"
#include "management_internal.h"
#include "network/network_service_internal.h"
#include "network/wifi/wifi_manager.h"
#include "kernel/runtime.h"
#include "kernel/service_executor.h"
#include <espnow.h>
#include <lunar_espnow_owner.h>
#include <esp_timer.h>
#ifdef ESP_PLATFORM
#include <esp_attr.h>
#endif
namespace lunaredu::lunaros {
namespace {
constexpr size_t header_bytes=22;
static_assert(header_bytes+kEspNowGroupPayloadBytes<=ESPNOW_DATA_LEN);
StaticMutex mutex;
struct Packet {std::array<uint8_t,kEspNowGroupPayloadBytes> bytes{};size_t size=0;uint32_t sequence=0;};
struct State {
  EspNowGroupWindow window;EspNowGroupStatus status;
  RegistrationArray<Packet> queue{true};size_t begin=0,count=0;
  uint32_t outgoing=0,incoming=0,deliveries=0;
  uint64_t deadline=0,last_epoch=0;
  bool transport=false,broadcast_owned=false;
};
// Software state is read by Network/Main owners, never by an ISR or DMA.
#if defined(ESP_PLATFORM) && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
EXT_RAM_BSS_ATTR
#endif
State state;
uint64_t now(){return uint64_t(esp_timer_get_time())/1000;}
bool empty(const std::array<uint8_t,6> &address){return std::all_of(address.begin(),address.end(),[](auto b){return b==0;});}
bool broadcast(const std::array<uint8_t,6> &address){return std::all_of(address.begin(),address.end(),[](auto b){return b==255;});}
bool authorized(){return state.status.active&&now()<state.deadline&&state.status.channel==wifi_observed_channel()&&
  state.window.authorized&&state.window.authorized(state.window.context);}
uint64_t get64(const uint8_t *p){uint64_t v=0;for(unsigned i=0;i<8;++i)v|=uint64_t(p[i])<<(i*8);return v;}
uint32_t get32(const uint8_t *p){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(p[i])<<(i*8);return v;}
esp_err_t receive(uint8_t *source,void *data,size_t size,wifi_pkt_rx_ctrl_t*) {
  if(!authorized()||state.window.role!=EspNowGroupRole::Receiver||!source||!data)return ESP_ERR_INVALID_STATE;
  const auto *p=static_cast<const uint8_t *>(data);
  StaticLockGuard lock(mutex);
  if(size<=header_bytes||size>header_bytes+kEspNowGroupPayloadBytes||std::memcmp(source,state.window.transmitter.data(),6)||
     std::memcmp(p,"LGR1",4)||std::memcmp(p+4,state.window.group.data(),6)||get64(p+10)!=state.window.epoch||
     !get32(p+18)||get32(p+18)<=state.incoming){++state.status.dropped;return ESP_ERR_INVALID_ARG;}
  // Consume the sequence even when the bounded consumer queue is full.
  state.incoming=get32(p+18);
  if(state.count==state.queue.size()){++state.status.dropped;return ESP_ERR_NO_MEM;}
  auto &packet=state.queue[(state.begin+state.count)%state.queue.size()];
  packet.size=size-header_bytes;packet.sequence=state.incoming;
  std::memcpy(packet.bytes.data(),p+header_bytes,packet.size);++state.count;++state.status.received;return ESP_OK;
}
bool close(std::string &error){
  {StaticLockGuard lock(mutex);state.status.active=false;state.status.retiring=true;state.count=state.begin=0;}
  if(state.transport){
    if(lunar_espnow_group_close()!=ESP_OK){error="espnow_group_crypto_retiring";return false;}
    state.transport=false;
  }
  if(state.broadcast_owned){
    const auto result=esp_now_del_peer(ESPNOW_ADDR_BROADCAST);
    if(result!=ESP_OK&&result!=ESP_ERR_ESPNOW_NOT_FOUND&&result!=ESP_ERR_ESPNOW_NOT_INIT){error="espnow_group_peer_retiring";return false;}
    state.broadcast_owned=false;
  }
  void (*retired)(void*)=nullptr;void *context=nullptr;
  {StaticLockGuard lock(mutex);
    if(state.deliveries){error="espnow_group_delivery_pending";return false;}
    retired=state.window.retired;context=state.window.context;
    state.window={};state.queue.clear();state.status.retiring=false;state.status.remaining_ms=0;
  }
  if(retired)retired(context);
  error.clear();return true;
}
struct Request {KernelRuntime *runtime;const EspNowGroupWindow *window=nullptr;const void *data=nullptr;size_t size=0;bool stop=false;};
void execute(void *opaque,ServiceOperationResult &result){
  const auto &request=*static_cast<Request *>(opaque);
  if(!require_network_runtime(*request.runtime,result.error)){result.ok=false;return;}
  if(request.stop){result.ok=close(result.error);return;}
  if(!admit_network_work(result.error)){result.ok=false;return;}
  if(request.window){
    const auto &window=*request.window;
    const auto channel=wifi_observed_channel();
    if(!espnow_ready()||internal::espnow_group_busy()||internal::espnow_responder_busy()||internal::espnow_client_busy()||
       network_maintenance_state()!=NetworkMaintenanceState::Normal||channel<1||channel>14||empty(window.group)||broadcast(window.group)||
       empty(window.transmitter)||broadcast(window.transmitter)||(window.transmitter[0]&1)||!window.key||!window.epoch||
       window.epoch<=state.last_epoch||state.status.generation==UINT32_MAX||
       (window.key&&std::all_of(window.key,window.key+32,[](auto b){return b==0;}))||
       window.duration_ms<100||window.duration_ms>60000||!window.authorized||
       !window.authorized(window.context)||(window.role!=EspNowGroupRole::Receiver&&window.role!=EspNowGroupRole::Transmitter)||
       (window.role==EspNowGroupRole::Receiver&&!window.receive)||
       (window.role==EspNowGroupRole::Transmitter&&window.transmitter!=espnow_status().local_mac)){
      result.ok=false;result.error="espnow_group_admission_rejected";return;
    }
    {StaticLockGuard lock(mutex);
      if(window.role==EspNowGroupRole::Receiver&&!state.queue.resize(8)){result.ok=false;result.error="espnow_group_queue_allocation_failed";return;}
      state.window=window;state.window.key=nullptr;state.status.active=false;
      state.status.retiring=true;state.status.role=window.role;state.status.group=window.group;state.status.channel=uint8_t(channel);++state.status.generation;
      state.outgoing=state.incoming=0;state.deadline=now()+window.duration_ms;state.last_epoch=window.epoch;}
    state.transport=true; // Partial initialization must also retire.
    const auto opened=lunar_espnow_group_open(window.group.data(),window.transmitter.data(),window.key);
    bool peer=true;
    if(opened==ESP_OK&&window.role==EspNowGroupRole::Transmitter){
      const bool existed=esp_now_is_peer_exist(ESPNOW_ADDR_BROADCAST);
      peer=internal::register_espnow_group_broadcast(result.error);state.broadcast_owned=peer&&!existed;
    }
    if(opened!=ESP_OK||!peer||espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_CONTROL_DATA,true,receive)!=ESP_OK){
      result.ok=false;std::string cleanup;if(!close(cleanup))result.error=cleanup;else result.error="espnow_group_transport_failed";return;
    }
    {StaticLockGuard lock(mutex);state.status.active=true;state.status.retiring=false;state.status.last_error.clear();}
    result.ok=true;return;
  }
  if(!authorized()||state.window.role!=EspNowGroupRole::Transmitter||!request.data||!request.size||request.size>kEspNowGroupPayloadBytes||state.outgoing==UINT32_MAX){
    result.ok=false;result.error="espnow_group_send_rejected";return;
  }
  std::array<uint8_t,header_bytes+kEspNowGroupPayloadBytes> packet{};
  std::memcpy(packet.data(),"LGR1",4);std::memcpy(packet.data()+4,state.window.group.data(),6);
  for(unsigned i=0;i<8;++i)packet[10+i]=uint8_t(state.window.epoch>>(i*8));
  const auto sequence=++state.outgoing;for(unsigned i=0;i<4;++i)packet[18+i]=uint8_t(sequence>>(i*8));
  std::memcpy(packet.data()+header_bytes,request.data,request.size);
  espnow_frame_head_t frame{};frame.broadcast=true;frame.group=true;frame.security=true;frame.retransmit_count=1;frame.filter_adjacent_channel=true;
  result.ok=::espnow_send(ESPNOW_DATA_TYPE_CONTROL_DATA,state.window.group.data(),packet.data(),header_bytes+request.size,&frame,0)==ESP_OK;
  {StaticLockGuard lock(mutex);if(result.ok)++state.status.sent;else{++state.status.dropped;state.status.last_error="espnow_group_send_failed";}}
  if(!result.ok)result.error="espnow_group_send_failed";
}
}
bool espnow_open_group(KernelRuntime &runtime,const EspNowGroupWindow &window,std::string &error){Request r{&runtime,&window};return runtime.invoke_executor(kNetworkServiceExecutor,execute,&r,error);}
bool espnow_send_group(KernelRuntime &runtime,const void *data,size_t size,std::string &error){Request r{&runtime,nullptr,data,size};return runtime.invoke_executor(kNetworkServiceExecutor,execute,&r,error);}
bool espnow_close_group(KernelRuntime &runtime,std::string &error){Request r{&runtime};r.stop=true;return runtime.invoke_executor(kNetworkServiceExecutor,execute,&r,error);}
EspNowGroupStatus espnow_group_status(){StaticLockGuard lock(mutex);auto s=state.status;s.supported=true;const auto time=now();
  s.remaining_ms=s.active&&time<state.deadline?uint32_t(state.deadline-time):0;
  if(s.active&&!s.remaining_ms){s.active=false;s.retiring=true;}return s;}
bool internal::espnow_group_busy(){StaticLockGuard lock(mutex);return state.status.active||state.status.retiring||state.transport||state.broadcast_owned;}
bool internal::stop_espnow_group(std::string &error){return close(error);}
void internal::poll_espnow_group(){if(state.status.retiring||(state.status.active&&!authorized())){std::string error;if(!close(error)){StaticLockGuard lock(mutex);state.status.last_error=error;}}}
void internal::drain_espnow_group(){
  for(unsigned i=0;i<8;++i){Packet packet;EspNowGroupWindow window;uint8_t channel=0;uint64_t deadline=0;
    {StaticLockGuard lock(mutex);if(!state.status.active||!state.count)return;packet=state.queue[state.begin];
      state.begin=(state.begin+1)%state.queue.size();--state.count;window=state.window;channel=state.status.channel;deadline=state.deadline;++state.deliveries;}
    // Caller owns the authorization context through successful close.
    if(window.authorized&&window.authorized(window.context)&&now()<deadline&&channel==wifi_observed_channel()&&window.receive)
      window.receive({window.group,window.transmitter,packet.sequence,packet.bytes.data(),packet.size},window.context);
    {StaticLockGuard lock(mutex);--state.deliveries;}
  }
}
}
#else
namespace lunaredu::lunaros {
bool espnow_open_group(KernelRuntime &,const EspNowGroupWindow &,std::string &e){e="espnow_group_disabled_in_build";return false;}
bool espnow_send_group(KernelRuntime &,const void *,size_t,std::string &e){e="espnow_group_disabled_in_build";return false;}
bool espnow_close_group(KernelRuntime &,std::string &e){e.clear();return true;}
EspNowGroupStatus espnow_group_status(){return {};}
bool internal::espnow_group_busy(){return false;}
bool internal::stop_espnow_group(std::string &e){e.clear();return true;}
void internal::poll_espnow_group(){}
void internal::drain_espnow_group(){}
}
#endif
