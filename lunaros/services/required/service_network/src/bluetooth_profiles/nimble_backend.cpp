#include "bluetooth_internal.h"
#include "network/bluetooth/bluetooth_lifecycle.h"
#include "sdkconfig.h"
#if CONFIG_LUNAROS_NETWORK_BLUETOOTH_PROFILES && CONFIG_BT_NIMBLE_ENABLED
#include <host/ble_hs.h>
#include <host/ble_gap.h>
#include <host/ble_hs_adv.h>
#include <esp_timer.h>
#include <algorithm>
#include <cstring>
#include <atomic>
namespace lunaredu::lunaros::bluetooth_service_internal {
namespace {
bool install(void *,std::string &e) {e.clear();return true;}
void sync(void *) {set_request_admission(true);std::lock_guard<std::mutex> lock(mutex);state.enabled=true;++state.revision;}
void reset(void *) {set_request_admission(false);std::lock_guard<std::mutex> lock(mutex);state.enabled=false;state.discovering=false;++state.revision;}
#if CONFIG_BT_NIMBLE_ROLE_OBSERVER
int scan(ble_gap_event *event,void *) {
  if(event->type==BLE_GAP_EVENT_DISC) {
    BluetoothDevice device;device.seen_ms=uint32_t(esp_timer_get_time()/1000);
    std::copy_n(event->disc.addr.val,6,device.address.begin());device.rssi=event->disc.rssi;
    ble_hs_adv_fields fields{};
    if(!ble_hs_adv_parse_fields(&fields,event->disc.data,event->disc.length_data)&&fields.name)
      std::memcpy(device.name.data(),fields.name,std::min(size_t(fields.name_len),device.name.size()-1));
    observe_device(device);
  } else if(event->type==BLE_GAP_EVENT_DISC_COMPLETE) {
    std::lock_guard<std::mutex> lock(mutex);state.discovering=false;++state.revision;
  }
  return 0;
}
#endif
bool stop(void *,std::string &e) {
  set_request_admission(false);
#if CONFIG_BT_NIMBLE_ROLE_OBSERVER
  if(ble_gap_disc_active()) {
    const int result=ble_gap_disc_cancel();
    if(result&&result!=BLE_HS_EALREADY){e="bluetooth_scan_stop_failed";return false;}
  }
#endif
  e.clear();return true;
}
void poll(void *) {
  Request request;if(!take_request(request))return;
  std::string error;
  if(!bluetooth_synchronized())error="bluetooth_disabled";
#if CONFIG_BT_NIMBLE_ROLE_OBSERVER
  else if(request.action==BluetoothAction::ScanBle) {
    uint8_t address_type=0;ble_gap_disc_params parameters{};
    parameters.itvl=0x80;parameters.window=0x40;parameters.filter_duplicates=1;
    if(ble_hs_id_infer_auto(0,&address_type)||ble_gap_disc(address_type,10000,&parameters,scan,nullptr))
      error="bluetooth_scan_start_failed";
    else {std::lock_guard<std::mutex> lock(mutex);state.discovering=true;++state.revision;}
  }
#endif
  else error="bluetooth_action_unsupported";
  report(error);
}
}
bool configure_backend(std::string &error) {
  // This host exposes discovery only. Provisioning retains its existing GAP
  // owner; do not claim HID, GATT data, pairing or audio profiles we don't install.
  static const BluetoothProfile profile{"bluetooth.apps",install,sync,reset,nullptr,nullptr,poll,nullptr,stop};
  if(!register_bluetooth_profile(profile,error))return false;
  std::lock_guard<std::mutex> lock(mutex);state.ble_supported=true;
#if CONFIG_BT_NIMBLE_ROLE_OBSERVER
  state.ble_scan_supported=true;
#endif
  return true;
}
}
#endif
