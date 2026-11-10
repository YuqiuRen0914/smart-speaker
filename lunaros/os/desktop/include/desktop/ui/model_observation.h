#pragma once
#include <cstdint>
#include <string_view>
namespace lunaredu::desktop {
// A product projects its Model into these UI facts. The IP view is borrowed
// only while comparing observations on the input owner, never retained in a
// frame or across callbacks. Product-specific state remains in its Model.
struct DesktopModelObservation {
  bool system_owned=false,wifi_connected=false,provisioning=false,platform_connected=false;
  bool battery_valid=false;
  int battery_percent=-1,battery_charge_state=0;
  uint16_t buttons=0;
  int16_t encoder_position=0;
  std::string_view local_ip;
  bool header_changed(const DesktopModelObservation &previous) const {
    return wifi_connected!=previous.wifi_connected || provisioning!=previous.provisioning ||
        local_ip!=previous.local_ip || battery_changed(previous);
  }
  bool shell_changed(const DesktopModelObservation &previous) const {
    return wifi_connected!=previous.wifi_connected || platform_connected!=previous.platform_connected ||
        battery_changed(previous);
  }
 private:
  bool battery_changed(const DesktopModelObservation &previous) const {
    return battery_valid!=previous.battery_valid || battery_percent!=previous.battery_percent ||
        battery_charge_state!=previous.battery_charge_state;
  }
};
// Opt-in mapping for the standard product system/input model. A product with
// another Model supplies its own projection; DesktopSessionHost knows no fields.
template<class Model> DesktopModelObservation observe_standard_desktop_model(const Model &model) {
  return {model.updating,model.wifi_connected,model.provisioning,model.platform_connected,
      model.battery.valid,model.battery.percent,int(model.battery.charge_state),
      model.buttons,model.encoder_position,model.local_ip};
}
namespace detail {
template<class Model> auto observe_model(Model &target,const Model &sample,int)
    -> decltype(target.observe_from(sample),void()) {target.observe_from(sample);}
template<class Model> void observe_model(Model &target,const Model &sample,long) {target=sample;}
}
// Always retain an owned observation. Models with versioned immutable facts
// may reuse those facts; render payloads must still own their captured values.
template<class Model> void observe_model(Model &target,const Model &sample) {
  if(&target!=&sample)detail::observe_model(target,sample,0);
}
}
