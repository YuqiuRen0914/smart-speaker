#include "lunaros_settings_bluetooth.h"
#if LUNAROS_SETTINGS_BLUETOOTH_COMPONENT
#include "lunaros_bluetooth.h"
#endif
namespace lunaredu::settings {
BluetoothObservationPorts bluetooth_observation_ports(){
#if LUNAROS_SETTINGS_BLUETOOTH_COMPONENT
  return {[](BluetoothPresentation &out){
    // Capture only on the input owner. Keep the service's device array off the
    // nested UI stack; immutable frames retain Settings' own projection.
    static lunaros::BluetoothSnapshot scratch;
    lunaros::bluetooth_snapshot_into(scratch);
    out.available=scratch.available;out.enabled=scratch.enabled;
    out.ble_scan_supported=scratch.ble_scan_supported;out.classic_supported=scratch.classic_supported;
    out.pairing_supported=scratch.pairing_supported;
    out.connected=scratch.ble_connected||scratch.spp_connected||scratch.music_connected;
    out.discovering=scratch.discovering;out.pairing=scratch.pairing;
    out.confirm_pending=scratch.confirm_pending;out.pairing_code=scratch.pairing_code;
    out.revision=scratch.revision;out.error=scratch.error;
    out.device_count=std::min(scratch.device_count,out.devices.size());
    for(size_t i=0;i<out.device_count;++i){
      out.devices[i]={scratch.devices[i].name,scratch.devices[i].rssi,scratch.devices[i].classic};
    }
  }};
#else
  return {};
#endif
}
bool queue_bluetooth_control(BluetoothControl control,std::string &error){
#if LUNAROS_SETTINGS_BLUETOOTH_COMPONENT
  using A=lunaros::BluetoothAction;
  A action;
  switch(control){
    case BluetoothControl::Pair:action=A::Pair;break;
    case BluetoothControl::ScanBle:action=A::ScanBle;break;
    case BluetoothControl::ScanClassic:action=A::ScanClassic;break;
    case BluetoothControl::ConfirmPair:action=A::ConfirmPair;break;
    case BluetoothControl::RejectPair:action=A::RejectPair;break;
    default:error="bluetooth_control_invalid";return false;
  }
  uint32_t id;return lunaros::request_bluetooth(action,0,"",id,error);
#else
  (void)control;error="bluetooth_profile_unavailable";return false;
#endif
}
}
