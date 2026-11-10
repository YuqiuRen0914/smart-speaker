#include "bluetooth_internal.h"
#include "kernel/service_executor.h"
#include "network/bluetooth/bluetooth_lifecycle.h"
#include "lunaros_network_control.h"
#include "music_output.h"
#include "lifecycle.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#if LUNAROS_NETWORK_BLUETOOTH_PROFILES
namespace lunaredu::lunaros {
namespace bluetooth_service_internal {
std::mutex mutex;
BluetoothSnapshot state;
KernelRuntime *runtime=nullptr;
std::array<char,27> device_name{};
std::array<Request,8> requests;
size_t request_count=0;
uint32_t next_id=1;
bool requests_allowed=true;
void set_request_admission(bool allowed) {
  std::lock_guard<std::mutex> lock(mutex);
  requests_allowed=allowed;
  if (!allowed) { request_count=0; requests={}; }
}
bool take_request(Request &request) {
  std::lock_guard<std::mutex> lock(mutex);
  if (!requests_allowed || network_bluetooth_stop_pending()) { request_count=0; requests={}; return false; }
  if(!request_count)return false;
  request=requests[0];for(size_t i=1;i<request_count;++i)requests[i-1]=requests[i];--request_count;
  state.request_id=request.id;return true;
}
void report(const std::string &error) { std::lock_guard<std::mutex> lock(mutex);std::snprintf(state.error.data(),state.error.size(),"%s",error.c_str());++state.revision; }
void observe_device(const BluetoothDevice &device) {
  std::lock_guard<std::mutex> lock(mutex);
  size_t index=state.device_count;
  for(size_t i=0;i<state.device_count;++i)if(state.devices[i].address==device.address&&state.devices[i].classic==device.classic){index=i;break;}
  if(index==state.devices.size()) {
    index=0;for(size_t i=1;i<state.device_count;++i)if(state.devices[i].seen_ms<state.devices[index].seen_ms)index=i;
  } else if(index==state.device_count)++state.device_count;
  auto merged=device;
  if(index<state.device_count&&!merged.name[0]&&state.devices[index].address==device.address)merged.name=state.devices[index].name;
  state.devices[index]=merged;++state.revision;
}
void receive(const uint8_t *data,size_t size) {
  std::lock_guard<std::mutex> lock(mutex);
  state.received_bytes+=uint32_t(size);
  const size_t count=std::min(size,state.received.size()-1);
  for(size_t i=0;i<count;++i)state.received[i]=data[i]>=32&&data[i]!=127?char(data[i]):'.';
  state.received[count]=0;state.dropped_bytes+=uint32_t(size-count);++state.revision;
}
}
bool request_bluetooth(BluetoothAction action,uint16_t value,const std::string &data,uint32_t &id,std::string &error) {
  using namespace bluetooth_service_internal;id=0;
  if(unsigned(action)>unsigned(BluetoothAction::MusicMedia)||data.size()>160||data.find('\0')!=std::string::npos) {error="bluetooth_request_invalid";return false;}
  if(action==BluetoothAction::Key&&(value<4||(value>0x65&&value<0xe0)||value>0xe7)){error="bluetooth_key_invalid";return false;}
  if((action==BluetoothAction::Media||action==BluetoothAction::MusicMedia)&&value!=0xcd&&value!=0xb5&&value!=0xb6&&value!=0xe9&&value!=0xea&&value!=0xe2){error="bluetooth_media_invalid";return false;}
  std::lock_guard<std::mutex> lock(mutex);
  if(!state.available){error="bluetooth_unavailable";return false;}
  if(network_bluetooth_stop_pending()){error="bluetooth_stop_pending";return false;}
  if(!requests_allowed){error="bluetooth_disabled";return false;}
  if((action==BluetoothAction::ScanClassic&&!state.classic_supported)||
     (action==BluetoothAction::ScanBle&&!state.ble_scan_supported)||
     ((action==BluetoothAction::Pair||action==BluetoothAction::ConfirmPair||action==BluetoothAction::RejectPair)&&!state.pairing_supported)||
     ((action==BluetoothAction::MusicOn||action==BluetoothAction::MusicOff||action==BluetoothAction::MusicMedia)&&!state.music_supported)||
     ((action==BluetoothAction::Key||action==BluetoothAction::ArmKeyboard||action==BluetoothAction::DisarmKeyboard)&&!state.hid_supported)||
     (action==BluetoothAction::Media&&!state.hid_supported&&!state.music_supported)||
     (action==BluetoothAction::SendData&&!state.data_supported)) {error="bluetooth_action_unsupported";return false;}
  if(action==BluetoothAction::MusicMedia&&(!state.enabled||!state.music_enabled||!state.music_connected||!state.music_control_available)){
    error="bluetooth_music_peer_required";return false;
  }
  if(request_count==requests.size()){error="bluetooth_request_queue_full";return false;}
  if(!next_id)++next_id;
  id=next_id++;
  Request request{action,value,{},id};std::copy(data.begin(),data.end(),request.data.begin());
  requests[request_count++]=request;error.clear();return true;
}
void bluetooth_snapshot_into(BluetoothSnapshot &out) {std::lock_guard<std::mutex> lock(bluetooth_service_internal::mutex);out=bluetooth_service_internal::state;}
BluetoothSnapshot bluetooth_snapshot() {BluetoothSnapshot out;bluetooth_snapshot_into(out);return out;}
BluetoothMusicSnapshot bluetooth_music_snapshot(){
  std::lock_guard<std::mutex> lock(bluetooth_service_internal::mutex);
  const auto &s=bluetooth_service_internal::state;
  return {s.available,s.enabled,s.music_enabled,s.music_connected,s.music_playing,
    s.music_control_available,s.error,s.music_supported};
}
bool configure_network_bluetooth_profiles(KernelRuntime &owner,const char *name,std::string &error) {
  using namespace bluetooth_service_internal;
  if(runtime||bluetooth_occupied()||!name||!*name||std::strlen(name)>26){error="bluetooth_configuration_invalid";return false;}
  std::snprintf(device_name.data(),device_name.size(),"%s",name);
  if(!configure_backend(error))return false;
  runtime=&owner;return true;
}
bool reset_network_bluetooth_profiles(std::string &error) {
  using namespace bluetooth_service_internal;
  if (bluetooth_occupied()) { error="bluetooth_retirement_pending"; return false; }
  { std::lock_guard<std::mutex> lock(mutex);
    if (state.available) { error="bluetooth_profiles_active"; return false; }
  }
  // A managed audio worker must release its admission before its Runtime is detached.
  if (!enable_music_output(false, error)) return false;
  std::lock_guard<std::mutex> lock(mutex);
  runtime=nullptr; request_count=0; requests={}; device_name={}; requests_allowed=true;
  const auto revision=state.revision+1; state={}; state.revision=revision;
  error.clear(); return true;
}
bool start_network_bluetooth_profiles(KernelRuntime &owner,std::string &error) {
  using namespace bluetooth_service_internal;
  if (!runtime && !configure_network_bluetooth_profiles(owner,"LunarOS",error)) return false;
  if (runtime != &owner) {error="bluetooth_runtime_conflict";return false;}
  std::lock_guard<std::mutex> lock(mutex);
  state.available=state.ble_supported||state.classic_supported;
  ++state.revision;error.clear();return true;
}
void stop_network_bluetooth_profiles() {
  using namespace bluetooth_service_internal;
  std::lock_guard<std::mutex> lock(mutex);
  state.available=false;request_count=0;++state.revision;
}
} // namespace lunaredu::lunaros
#else
namespace lunaredu::lunaros {
bool request_bluetooth(BluetoothAction,uint16_t,const std::string &,uint32_t &id,std::string &error) {
  id=0;error="bluetooth_profile_unavailable";return false;
}
void bluetooth_snapshot_into(BluetoothSnapshot &out) {
  static const BluetoothSnapshot unavailable{};out=unavailable;
}
BluetoothSnapshot bluetooth_snapshot(){BluetoothSnapshot out;bluetooth_snapshot_into(out);return out;}
BluetoothMusicSnapshot bluetooth_music_snapshot(){return {};}
bool configure_network_bluetooth_profiles(KernelRuntime &,const char *,std::string &error) {
  error="bluetooth_profile_unavailable";return false;
}
bool start_network_bluetooth_profiles(KernelRuntime &,std::string &error){error.clear();return true;}
void stop_network_bluetooth_profiles(){}
bool reset_network_bluetooth_profiles(std::string &error){error.clear();return true;}
}
#endif
