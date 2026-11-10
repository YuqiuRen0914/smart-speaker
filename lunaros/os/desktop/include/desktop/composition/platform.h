#pragma once
#include "desktop/composition/selection.h"

namespace lunaredu::lunaros {
// Selection generates catalog, menus and UI bindings once. Hardware facts,
// native-App policy and rendering stay in the selected product's ports.
template<class Product, class ModelType, class StateType, class CommandType,
         class ApplicationsType, class PortsType>
struct DesktopPlatformDefaults : Product {
  using Model = ModelType; using State = StateType; using Command = CommandType;
  using Applications = ApplicationsType; using ApplicationPorts = PortsType;
  using Payload = typename std::decay_t<decltype(Product::selection())>::Presentation;
  static const auto &catalog() { return Product::selection().catalog(); }
  static desktop::DesktopUiOptions ui_options() {
    const auto &selection = Product::selection(); const auto owners = selection.menu_owners();
    desktop::DesktopUiOptions result{Product::shell_id, Product::settings_id, owners.data(), owners.size(),
            selection.catalog().launchers().size(), Product::rendering_options()};
    result.home_widgets=selection.home_widgets();result.home_widget_count=selection.home_widget_count;
    return result;
  }
  template<class Bindings> static Bindings bindings() {
    return Product::selection().template bindings<Bindings>(Product::shell_id);
  }
};

// Default binding for a retained UiRenderHost. A product supplies only the
// system presentation capture; task/frame budgets remain setup() arguments.
template<auto &Screen, auto Capture, auto Policy=nullptr> struct DesktopDisplayPort {
  static bool ready() { return Screen.ready(); }
  static auto &graphics() { return Screen.graphics(); }
  static void wake() { Screen.wake(); }
  static void request_redraw() { Screen.request_redraw(); }
  static bool take_redraw() { return Screen.take_redraw(); }
  static auto status() { return Screen.status(); }
  template<class State> static auto drawing_policy(const State &state,bool system) {
    if constexpr(std::is_same_v<decltype(Policy),std::nullptr_t>)return desktop::drawing_policy(state,system);
    else return Policy(state,system);
  }
  template<class State,class Model,class Payload>
  static void publish(desktop::RenderKind kind,const State &state,const Model &model,
                      bool force,Payload &&payload,int diagnostic=0,uint32_t ticket=0) {
    if constexpr(std::is_invocable_v<decltype(Capture),const State &,const Model &>)
      Screen.publish(kind,state,Capture(state,model),std::forward<Payload>(payload),force,diagnostic,ticket);
    else Screen.publish(kind,state,Capture(model),std::forward<Payload>(payload),force,diagnostic,ticket);
  }
};
}
