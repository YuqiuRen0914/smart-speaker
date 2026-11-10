#pragma once
#include <array>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <cstdint>
namespace lunaredu::lunaros::graphics {class Scene;}

namespace lunaredu::settings {
enum class BluetoothControl { Pair, ScanBle, ScanClassic, ConfirmPair, RejectPair };
struct BluetoothDevicePresentation {
  std::array<char,40> name{};
  int8_t rssi=-127;
  bool classic=false;
  bool operator==(const BluetoothDevicePresentation &o)const{return name==o.name&&rssi==o.rssi&&classic==o.classic;}
};
struct BluetoothPresentation {
  bool ble_scan_supported=false,classic_supported=false,pairing_supported=false;
  bool available=false,enabled=false,connected=false,discovering=false,pairing=false,confirm_pending=false;
  uint32_t revision=0,pairing_code=0;
  size_t device_count=0;
  std::array<BluetoothDevicePresentation,24> devices{};
  std::array<char,96> error{};
  bool operator==(const BluetoothPresentation &o)const{
    if(ble_scan_supported!=o.ble_scan_supported||classic_supported!=o.classic_supported||pairing_supported!=o.pairing_supported)return false;
    if(available!=o.available||enabled!=o.enabled||connected!=o.connected||discovering!=o.discovering||
      pairing!=o.pairing||confirm_pending!=o.confirm_pending||pairing_code!=o.pairing_code||device_count!=o.device_count||error!=o.error)return false;
    for(size_t i=0;i<std::min(device_count,devices.size());++i)if(!(devices[i]==o.devices[i]))return false;
    return true;
  }
};
// Optional backend facts. Settings works with Network's radio switch even on
// products that do not select the Bluetooth profile service.
struct BluetoothObservationPorts {
  void (*capture)(BluetoothPresentation &)=nullptr;
};
BluetoothObservationPorts bluetooth_observation_ports();
bool queue_bluetooth_control(BluetoothControl,std::string &error);
bool build_bluetooth_surface(lunaros::graphics::Scene &,const BluetoothPresentation &,unsigned scroll,bool auto_hide,unsigned focus=0,bool busy=false);
class BluetoothObservation {
 public:
  void bind(BluetoothObservationPorts ports){ports_=ports;}
  bool poll(int64_t now_us,const std::string &error={}) {
    if(!ports_.capture)return false;
    if(value_&&now_us-last_us_<200000&&error==error_)return false;
    last_us_=now_us;
    if(!scratch_)scratch_=std::make_unique<BluetoothPresentation>();
    ports_.capture(*scratch_);
    if(!error.empty())std::snprintf(scratch_->error.data(),scratch_->error.size(),"%s",error.c_str());
    if(value_&&*value_==*scratch_&&error_==error)return false;
    auto next=std::make_shared<BluetoothPresentation>(*scratch_);
    value_=std::move(next);error_=error;return true;
  }
  const std::shared_ptr<const BluetoothPresentation> &snapshot()const{return value_;}
 private:
  BluetoothObservationPorts ports_;
  std::unique_ptr<BluetoothPresentation> scratch_;
  std::shared_ptr<const BluetoothPresentation> value_;
  std::string error_;
  int64_t last_us_=0;
};
}
