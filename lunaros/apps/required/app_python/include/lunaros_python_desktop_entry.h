#pragma once
#include "desktop/composition/selection.h"
#include "lunaros_python_ui_snapshot.h"
#include "lunaros_python_application.h"
#include "lunaros_python_desktop_session.h"
namespace lunaredu::python {
template<class Host> class SelectedDesktopSession : public lunaros::DesktopSessionDefaults {
 public:
  using Configuration = typename Host::Configuration;
  explicit SelectedDesktopSession(Host &host) : host_(host), session_(host.ui()) {}
  void bind(lunaros::ApplicationContext *device) { session_.configure(device, Configuration::shell_id); }
  void foreground_tick() { if(!external_drive_)session_.tick(); }
  // The existing execution owner declares and releases its drive lease.
  // A one-off manual tick never permanently disables normal UI maintenance.
  void external_drive(bool active) { external_drive_=active; }
  void engine_tick() { session_.tick(); }
  bool widgets() const { return host_.state().page==kDesktopView; }
  bool temporary_view(app::ViewRoute page) const { return page == kDesktopView; }
  bool project_foreground(bool active, std::string &error) {
    error.clear(); if (!host_.ui().ready_ui()) { error = "desktop_ui_not_ready"; return false; }
    auto page = host_.ui().previous_ui_route();
    if (host_.temporary_view(page)) page = desktop::kShellView;
    const bool projected = session_.project_foreground(active, page);
    if (active) session_.tick();
    return projected;
  }
  template<class Base> struct Context : Base {
    Context(Base base, SelectedDesktopSession &session) : Base(std::move(base)), owner(session) {}
    decltype(auto) python_presentation() const { return owner.session_.view(); }
    bool launch_foreground(const char *id, std::string &error) { return owner.session_.launch(id, error); }
    void exit_python(bool home) { owner.session_.request_exit(home); }
    bool python_exit_action() {
      bool exit=false; lunaros::graphics::Action action;
      while(owner.host_.ui().take_action(action)) exit=exit||action.id==kExitAction;
      return exit;
    }
    SelectedDesktopSession &owner;
  };
 private:
  Host &host_;
  DesktopSession session_;
  bool external_drive_=false;
};
// The engine's existing runtime owner declares SystemApplication. This entry
// selects its desktop metadata and UI without starting or preparing the VM.
struct DesktopEntry : lunaros::DesktopEntryDefaults {
  using Presentation = RenderPresentation;
  template<class Host> using Session = SelectedDesktopSession<Host>;
  static desktop::DesktopApplication tile() { return {kApplicationId, "编程", desktop::DesktopIcon::Code, 0x45df}; }
  template<class Bindings> static auto binding() { return ui_binding<Bindings>(); }
  template<class Host> static void external_drive(Host &host,bool active) { host.template with_app<DesktopEntry>([&](auto &session) { session.external_drive(active); }); }
  template<class Host> static void tick(Host &host) { host.template with_app<DesktopEntry>([](auto &session) { session.engine_tick(); }); }
  template<class Host> static bool foreground(Host &host, bool active, std::string &error) {
    bool projected = false;
    if (!host.template with_app<DesktopEntry>([&](auto &session) { projected = session.project_foreground(active, error); })) error = "python_desktop_not_selected";
    return projected;
  }
};
}
