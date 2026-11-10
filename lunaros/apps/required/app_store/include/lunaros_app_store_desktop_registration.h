#pragma once
#include "desktop/composition/applications.h"
#include "desktop/composition/selection.h"
#include "lunaros_app_manager_application.h"
#include "lunaros_app_store_ui_snapshot.h"
#include "lunaros_app_store_desktop_host.h"
#include "lunaros_app_store_desktop_input.h"
#include "lunaros_app_store_projection.h"
#include <optional>
namespace lunaredu::lunaros::apps::manager {
template<class Host> class SelectedDesktopSession : public DesktopSessionDefaults {
 public:
  using Configuration = typename Host::Configuration;
  explicit SelectedDesktopSession(Host &host) : host_(host), session_(host.ui(), Configuration::catalog().launchers().data(), Configuration::catalog().launchers().size()) {}
  bool bind_service(ApplicationsService &service) { return session_.bind(service); }
  bool prepare(KernelRuntime &runtime, std::string &error) {
    if (session_.setup(runtime, Configuration::version())) return true;
    error = "desktop_package_host_setup_failed"; return false;
  }
  bool recovery_required() const { return !session_.preparation().ready(); }
  template<class Ports> void application_ports(Ports &ports) { host_.application_bindings().package_ports(ports, session_); }
  void poll(bool allowed) { session_.poll(allowed); }
  bool widgets() const { return host_.state().page == kStoreView; }
  void cancel_input() { (void)session_.cancel_input(); }
  void release_transient_frames() { session_.release_frame(); }
  void destination_committed(app::ViewRoute page) { if (page == desktop::kShellView) release_transient_frames(); }
  bool temporary_view(app::ViewRoute page) const { return page == kDownloadedView; }
  std::string running_application() const { return host_.state().page == kDownloadedView ? session_.running_id() : std::string(); }
  bool command(const char *line) { return session_.queue_command(line); }
  bool declare_management(DeviceProjectionRegistry &registry,std::string &error) {
    if(!management_)management_.emplace();
    return management_->declare(registry,management_ports(),error);
  }
  PackageManagementPorts management_ports() {
    auto ports=session_.management_ports();
    ports.launch_context=this;
    ports.launch=[](void *p,const std::string &id,std::string &error) {
      if(static_cast<SelectedDesktopSession *>(p)->launch_downloaded(id.c_str()))return true;
      error="downloaded_foreground_admission_failed";return false;
    };
    return ports;
  }
  bool launch_downloaded(const char *id) {
    return session_.open_downloaded(id, [&](const std::string &selected, std::string &error) {
      desktop::DesktopUiHost::Transition transition(host_.ui()); return host_.applications().open_guest(selected, error);
    });
  }
  template<class Base> struct Context : Base {
    Context(Base base, SelectedDesktopSession &session) : Base(std::move(base)), owner(session) {}
    auto store_presentation() const { return owner.session_.presentation(); }
    auto downloaded_presentation() const { return owner.session_.downloaded_presentation(); }
    bool open_store() { return owner.session_.open_store(true); }
    bool package_input() {
      auto input = package_desktop_input(Configuration::logical_input(this->model), *this);
      input.header_changed = this->host.header_changed(this->model, this->host.state().last_model);
      owner.session_.update(input); this->host.record_input(this->model); return true;
    }
    SelectedDesktopSession &owner;
  };
 private:
  Host &host_;
  PackageDesktopHost session_;
  std::optional<PackageManagementProjection> management_;
};
struct DesktopRegistration {
  bool operator()(SystemApplication &application,app::AppController &controller,DesktopApplicationPorts ports,std::string &error) const {
    return application.declare(controller,{ports.context,ports.prepare,ports.enter,ports.leave},ports.package_preparation,error);
  }
};
inline DesktopAppDescriptor desktop_registration() {
  return {kApplicationId,DesktopAppOwner<SystemApplication,DesktopRegistration>::create};
}
struct DesktopEntry : lunaros::DesktopEntryDefaults {
  using Presentation = RenderPresentation;
  template<class Host> using Session = SelectedDesktopSession<Host>;
  static desktop::DesktopApplication tile() { return {kApplicationId, "应用商店", desktop::DesktopIcon::Store, 0x45df,desktop::DesktopAppKind::Application,{},nullptr,desktop::DesktopAppCategory::System}; }
  static auto provider() { return desktop_registration(); }
  template<class Host> static bool declare_management(Host &host,DeviceProjectionRegistry &registry,std::string &error) {
    bool declared=false;
    if(!host.template with_app<DesktopEntry>([&](auto &session){declared=session.declare_management(registry,error);})) {
      error="package_management_session_not_selected";return false;
    }
    return declared;
  }
  template<class Host> static PackageManagementPorts management_ports(Host &host) {
    PackageManagementPorts result;
    host.template with_app<DesktopEntry>([&](auto &session){result=session.management_ports();});
    return result;
  }
  template<class Host> static bool bind_service(Host &host, ApplicationsService &service) {
    bool bound = false;
    host.template with_app<DesktopEntry>([&](auto &session) { bound = session.bind_service(service); });
    return bound;
  }
  template<class Bindings> static auto binding() { return ui_binding<Bindings>(); }
  template<class Host> static bool command(Host &host, const char *line) {
    bool queued = false; host.template with_app<DesktopEntry>([&](auto &session) { queued = session.command(line); }); return queued;
  }
};
// Store and the downloaded foreground share one selected package session.
// Selecting either entry provides the backend; selecting both creates it once.
struct DownloadedDesktopEntry : DesktopDownloadedEntry {
  template<class Host> using Session = SelectedDesktopSession<Host>;
};
}
