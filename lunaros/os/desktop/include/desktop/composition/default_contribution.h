#include "app/lifecycle/app_controller_view.h"
#pragma once
#include "app/lifecycle/hosted_application.h"
#include "desktop/ui/host.h"
#include "desktop/ui/status.h"
#include "desktop/ui/render_frame.h"
#include <variant>

namespace lunaredu::lunaros {
// Standard desktop policy for products with no native App. Selected Apps own
// their sessions and snapshots; this contribution owns no business state.
class DefaultDesktopContribution {
 public:
  enum class Paint { Full };
  using Lifecycle = app::HostedApplicationPorts;
  static constexpr std::monostate no_action{};
  static constexpr Paint full_paint = Paint::Full;
  std::monostate state() { return {}; }
  DefaultDesktopContribution &view() { return *this; }
  void configure(Lifecycle lifecycle) { lifecycle_ = lifecycle; }
  template<class Profile> void control_profile(Profile) {}
  template<class Model> void initialize_model(Model &) {}
  bool prepare(const char *id, std::string &error) {
    return !lifecycle_.prepare || lifecycle_.prepare(lifecycle_.context,id,error);
  }
  bool backend_entered(const char *id, std::string &error) {
    return !lifecycle_.enter || lifecycle_.enter(lifecycle_.context,id,error);
  }
  bool backend_leaving(const char *id, std::string &error) {
    return !lifecycle_.leave || lifecycle_.leave(lifecycle_.context,id,error);
  }
  template<class Ports,class Host> void application_ports(Ports &,Host &) {}
  template<class Base> struct InputContext : Base {};
  template<class Base> struct CaptureContext : Base {};
  template<class Base> struct EntryContext : Base {};
  template<class Base> struct LaunchContext : Base {};
  template<class Host> bool active(Host &) { return false; }
  template<class Host> bool control_active(Host &) { return false; }
  template<class Host> bool consumes_controls(Host &) { return true; }
  template<class Host> std::monostate capabilities(Host &) { return {}; }
  template<class Model> bool blocked(const Model &) const { return false; }
  template<class Model> bool presentation_changed(const Model &,const Model &) const { return false; }
  template<class Model> app::ViewRoute capture_route(app::ViewRoute page,const Model &) const { return page; }
  template<class Host,class Model> desktop::DesktopUiPolicy policy(Host &,const Model &) { return {}; }
  template<class Model> void observe_release(bool,const Model &) {}
  template<class Host,class Model> void prepare_destination(Host &,const Model &) {}
  template<class Host,class Model> bool safety(Host &,const Model &) { return false; }
  template<class Host,class Model> bool shell_action(Host &,const char *,const Model &) { return false; }
  template<class Host,class Model> bool launcher_back(Host &,const Model &) { return false; }
  template<class Host,class Model> bool back_application(Host &,const Model &) { return false; }
  template<class Host> void exited(Host &) {}
  template<class Host> void rearm_destination(Host &) {}
  template<class Host,class Model> bool observe_menu(Host &,const Model &,int64_t) { return false; }
  bool owns(app::ViewRoute) const { return false; }
  void rearm() {}
  app::ViewRoute qualify_destination(app::ViewRoute page) const { return page; }
  void destination_committed(app::ViewRoute) {}
  template<class Action> bool widget_action(app::ViewRoute,Action,bool &,bool &) { return false; }
  template<class Workflow> bool report_selection(Workflow,app::ViewRoute,bool) { return false; }
  template<class Host> void status_into(Host &host,desktop::UiStatus &result) {
    const auto &state=host.state(); const auto *drawing=desktop::drawing_application(state);
    const auto *owner=host.applications().controller();
    desktop::capture_ui_status(result,state.desktop,host.render_policy(host.system_owned()),state.ready,
        state.page.name,state.navigation.launcher_index,drawing?drawing->id:nullptr,
        owner?lunaredu::app::AppControllerView{owner}.active_application_id():nullptr,host.running_application());
  }
  template<class Host> desktop::UiStatus status(Host &host) {
    desktop::UiStatus result;status_into(host,result);return result;
  }
 private:
  Lifecycle lifecycle_{};
};
template<class Model> struct DefaultDesktopState : desktop::UiHostState {
  explicit DefaultDesktopState(std::monostate) {}
  std::monostate pending_action{};
  Model last_model{};
};
}
