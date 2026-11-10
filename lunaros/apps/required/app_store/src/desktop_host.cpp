#include "lunaros_app_store_desktop_host.h"
#include "desktop/application/desktop.h"
#include "desktop/application/catalog.h"
#include "desktop/graphics/icon_resource.h"
#include "desktop/ui/render_host.h"
#include "lunaros_app_manager_application.h"
#include "lunaros_app_store_desktop.h"
#include <cstring>
#include <cstdio>

namespace lunaredu::lunaros::apps {
namespace apps = lunaredu::lunaros::apps;
bool PackageDesktopHost::setup(lunaros::KernelRuntime &runtime, const char *version) {
  if (!runtime_ || !runtime_->owned_by(runtime)) return false;
  version_ = version ? version : "";
  std::string error;
  if (!ensure_available(error)) log(error);
  const std::vector<apps::Application> empty;
  if (!catalog_.apply(preparation_.ready() ? runtime_->installed() : empty,true,[&](auto next){return port_.publish_catalog(std::move(next));})) return false;
  return true; // Optional package preparation cannot retire the built-in desktop.
}
bool PackageDesktopHost::ensure_available(std::string &error) {
  if (!runtime_) { error="applications_service_missing"; return false; }
  if (preparation_.ready()) return runtime_->prepare(error);
  return preparation_.ensure([&](std::string &failure) {
    if (!runtime_->prepare(failure)) return false;
    store_.setup(*runtime_);
    ui_.store = std::make_shared<const apps::StoreSnapshot>(store_.snapshot());
    catalog_.invalidate();
    return true;
  }, error);
}
void PackageDesktopHost::capture_view() {
  if (!preparation_.ready()) return;
  if (!ui_.store || ui_.store->generation != store_.snapshot().generation)
    ui_.store = std::make_shared<const apps::StoreSnapshot>(store_.snapshot());
  auto &view = ui_.store_ui;
  apps::manager::reconcile_selection(*ui_.store, view);
  if(!store_visible())return;
  const auto key = apps::manager::view_key(*ui_.store, view);
  if (store_visible() && ui_.store_view_key != key) {
    port_.invalidate_input(); ui_.store_view_key = key;
  }
}
void PackageDesktopHost::update_catalog(bool force) {
  if (!preparation_.ready() || !catalog_.dirty()) return;
  const bool allowed = force || port_.catalog_publish_allowed();
  if (catalog_.apply(runtime_->installed(),allowed,[&](auto next){return port_.publish_catalog(std::move(next));})) {
    port_.request_ui_redraw();
  }
}
void PackageDesktopHost::poll(bool commands_allowed) {
  if (preparation_.ready() && downloaded_.refresh()) {
    ui_.app_frame = downloaded_.frame(); catalog_.invalidate();
  }
  if (preparation_.ready() && store_.poll()) {
    catalog_.observe(runtime_->installed());
    capture_view();
    if (store_visible()) port_.render_ui();
  }
  update_catalog();
  if (commands_allowed) process_command();
}
void PackageDesktopHost::action(uint32_t action) {
  std::string error;
  if (!ensure_available(error)) { log(error); return; }
  const auto result = apps::manager::dispatch(ui_.store_ui, store_, *runtime_, action);
  if (result.catalog_changed) catalog_.invalidate();
  if (!result.launch_id.empty()) port_.launch_ui(result.launch_id.c_str(), !result.builtin);
}
bool PackageDesktopHost::queue_command(const char *command) {
  if (!command || std::strlen(command) > 64) return false;
  std::lock_guard<std::mutex> lock(command_mutex_);
  if (!command_.empty()) return false;
  command_ = command;command_pending_.store(true,std::memory_order_release);return true;
}
manager::DesktopInteractionResult PackageDesktopHost::interact(const manager::DesktopInteractionInput &input) {
  const auto result = interaction_.update(ui_.store_ui, store_.snapshot(), port_, input, this,
      [](void *p, uint32_t action) { static_cast<PackageDesktopHost *>(p)->action(action); },
      [](void *p) { return static_cast<PackageDesktopHost *>(p)->store_visible(); });
  capture_view();
  return result;
}
void PackageDesktopHost::process_command() {
  if(!command_pending_.load(std::memory_order_acquire))return;
  std::string command;
  { std::lock_guard<std::mutex> lock(command_mutex_); command.swap(command_);command_pending_.store(false,std::memory_order_release); }
  if (command.empty()) return;
  std::string error;
  if (!ensure_available(error)) { log(error); unavailable(); return; }
  auto result = runtime_console_.dispatch(*runtime_, command);
  if (!result.handled) result = store_console_.dispatch(store_, ui_.store_ui, command);
  for (const auto &record : result.records) log(record);
  if (result.catalog_changed) catalog_.invalidate();
  switch (result.action) {
    case apps::ApplicationHostAction::ShowStore: open_store(); break;
    case apps::ApplicationHostAction::Launch: port_.launch_ui(result.launch_id.c_str(), true); break;
    case apps::ApplicationHostAction::Home: port_.home(); break;
    case apps::ApplicationHostAction::None: break;
  }
  if (result.frame_changed) {
    downloaded_.refresh(); ui_.app_frame = downloaded_.frame(); port_.render_ui();
  }
  if (command.rfind("save", 0) == 0) return;
  capture_view();
  const auto &snapshot = store_.snapshot();
  log("appstore discovered=" + std::to_string(snapshot.discovered.size()) +
      " installed=" + std::to_string(snapshot.installed.size()) + " running=" + runtime_->running_id() +
      " phase=" + std::to_string(static_cast<unsigned>(snapshot.phase)) + " error=" + snapshot.error);
  for (const auto &app : snapshot.installed) {
    log("installed id=" + app.id + " version=" + app.version + " bytes=" + std::to_string(app.size) + " sha256=" + app.sha256);
  }
}
void PackageDesktopHost::log(const std::string &record) { port_.log_ui("appstore",record); }
void PackageDesktopHost::unavailable() {
  port_.unavailable("应用存储","存储或应用目录暂不可用，请返回后重试");
}
ApplicationDataPorts PackageDesktopHost::data_ports() {
  auto ports=runtime_ ? application_data_ports(*runtime_) : ApplicationDataPorts{};
  ports.prepare_context=this;
  ports.prepare=[](void *p,std::string &error){return static_cast<PackageDesktopHost *>(p)->ensure_available(error);};
  return ports;
}
PackageManagementPorts PackageDesktopHost::management_ports() {
  return {this,
    [](void *p)->const StoreSnapshot * {
      auto &self=*static_cast<PackageDesktopHost *>(p);
      return self.preparation_.ready()&&self.runtime_&&self.runtime_->ready()?&self.store_.snapshot():nullptr;
    },
    [](void *p){return static_cast<PackageDesktopHost *>(p)->running_id().c_str();},
    [](void *p,std::string &error) {
      auto &self=*static_cast<PackageDesktopHost *>(p);
      if(self.port_.ui_blocked()){error="desktop_controls_blocked";return false;}
      if(!self.ensure_available(error))return false;
      if(self.store_.refresh())return true;
      error=self.store_.snapshot().error;return false;
    },
    [](void *p,const std::string &id,std::string &error) {
      auto &self=*static_cast<PackageDesktopHost *>(p);
      if(self.port_.ui_blocked()){error="desktop_controls_blocked";return false;}
      if(self.store_.install(id))return true;
      error=self.store_.snapshot().error;return false;
    },
    [](void *p,const std::string &id,bool data,std::string &error) {
      auto &self=*static_cast<PackageDesktopHost *>(p);
      if(self.port_.ui_blocked()){error="desktop_controls_blocked";return false;}
      if(!self.runtime_||!self.runtime_->ready()){error="application_directory_unavailable";return false;}
      if(self.running_id()==id){error="exit_active_package_first";return false;}
      if(!self.store_.uninstall(id,data,error))return false;
      self.capture_view();return true;
    },nullptr,nullptr};
}
app::ApplicationStartResult PackageDesktopHost::open_backend(const std::string &id,std::string &error) {
  // Startup may only observe the prepared package owner. Retrying storage
  // setup/catalog reads belongs to Store preparation, not the foreground tick.
  if(!preparation_.ready() || !runtime_ || !runtime_->ready()){error="application_directory_unavailable";return app::ApplicationStartResult::Failed;}
  const auto result=downloaded_.open(id,error);
  if(result!=app::ApplicationStartResult::Started)return result;
  port_.display_application(id);ui_.app_frame=downloaded_.frame();port_.rearm_ui();return result;
}
bool PackageDesktopHost::open_store(bool refresh) {
  if(port_.ui_blocked())return false;
  if(refresh)ui_.store_ui={};
  std::string error;
  if(!ensure_available(error)){log(error);unavailable();return false;}
  if(!port_.enter_ui(manager::ui_entry()))return false;
  port_.select_application(manager::kApplicationId);
  interaction_.reset();release_frame();capture_view();port_.render_ui();
  if(refresh){(void)store_.refresh();capture_view();}
  return true;
}
DownloadedPresentation PackageDesktopHost::downloaded_presentation() const {
  return {ui_.app_frame,port_.displayed_application_title()};
}
bool PackageDesktopHost::update(const PackageDesktopInput &input) {
  bool changed=port_.take_ui_redraw();
  bool request_home=false;
  if(port_.ui_route()==kDownloadedView){
    const auto result=downloaded_.update_desktop(input.guest,[&](int x,int y){return port_.logical_point(x,y);});
    changed|=result.changed;ui_.app_frame=downloaded_.frame();request_home=result.request_home;
    if(result.completed){capture_view();open_store();changed=true;}
    if(result.completed&&input.guest.back)request_home=true;
  }else{
    const auto point=port_.logical_point(input.guest.touch.x,input.guest.touch.y);
    auto touch=input.guest.touch;touch.x=uint16_t(point.x);touch.y=uint16_t(point.y);
    const auto result=interact(
        {touch,input.now_us,input.guest.controls.encoder,input.x,input.y,input.guest.command,
         input.guest.reserved_contact,input.guest.controls.confirm,input.guest.back,
         input.guest.controls.axes[2],input.guest.controls.axes[3],input.guest.controls.fresh});
    if(result.wake_graphics)port_.wake_ui();
    changed|=result.changed||input.header_changed;request_home=result.request_home;
  }
  if(request_home)port_.home();else if(changed)port_.render_ui();
  return true;
}
} // namespace lunaredu::lunaros::apps
