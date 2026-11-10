#pragma once
#include "desktop/ui/host.h"
#include "desktop/ui/logical_input.h"
#include <type_traits>
#include <utility>
#include <cstdlib>
namespace lunaredu::desktop {
template<class Model,class=void>struct HasWifiSsid:std::false_type {};
template<class Model>struct HasWifiSsid<Model,std::void_t<decltype(std::declval<const Model &>().wifi_ssid.c_str())>>:std::true_type {};
struct PhysicalControlMap {
  uint16_t first=0,second=0,confirm=0,back=0,left_stick=0,right_stick=0;
};
// One physical map feeds navigation, packages and games. Model observations
// remain borrowed; input conversion never acknowledges a delta or touches I/O.
template<auto &Map> struct MappedDesktopInput {
  struct Keys { bool first,second; };
  template<class Model> static Keys keys(const Model &model) {
    return {bool(model.buttons&Map.first),bool(model.buttons&Map.second)};
  }
  template<class Model,class State> static DesktopUiSample input_sample(const Model &model,const State &state,int64_t now) {
    auto sample=DesktopUiSample{{model.touch,{model.buttons,Map.confirm,Map.back,model.encoder_position,false},model.normalized_axes[0],now,model.updating},
        -model.normalized_axes[1],bool((model.buttons&Map.left_stick)&&!(state.last_buttons&Map.left_stick)),false,
        model.wifi_connected,model.platform_available,model.platform_connected};
    if constexpr(HasWifiSsid<Model>::value)sample.wifi_ssid=model.wifi_ssid.c_str();
    sample.input.secondary_horizontal=model.normalized_axes[2];
    sample.input.secondary_vertical=-model.normalized_axes[3];
    sample.input.axes_fresh=model.input_fresh;
    return sample;
  }
  template<class Model> static LogicalInput logical_input(const Model &model) {
    return {model.input_fresh,model.normalized_axes,bool(model.buttons&Map.confirm),bool(model.buttons&Map.back),
        bool(model.buttons&Map.left_stick),bool(model.buttons&Map.right_stick),bool(model.buttons),model.touch};
  }
};
}
