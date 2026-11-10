#pragma once
#include "lunaros_applications_service_id.h"
#include "desktop/ui/entry.h"
#include "desktop/application/desktop.h"
#include "app/lifecycle/application_preparation.h"
#include "desktop/application/downloaded_surface.h"
#include "lunaros_app_store_runtime_console.h"
#include "lunaros_app_store.h"
#include "lunaros_app_store_console.h"
#include "lunaros_app_manager_view.h"
#include "lunaros_app_store_desktop_interaction.h"
#include <mutex>
#include <atomic>
#include <cstring>
#include "desktop/ui/host_port.h"
#include "lunaros_app_store_ui_state.h"
#include "lunaros_app_store_ui_snapshot.h"
#include "desktop/application/downloaded_ui_snapshot.h"
#include "app/package/application_data_ports.h"
#include "lunaros_package_management.h"

namespace lunaredu::lunaros::apps {
struct PackageDesktopInput {
  DownloadedDesktopInput guest;
  int64_t now_us=0;
  int x=0,y=0;
  bool header_changed=false;
};
// Shared composition of the OS package owner and Store App. All methods except
// queue_command run on the existing input executor; no worker is introduced.
class PackageDesktopHost {
 public:
  PackageDesktopHost(desktop::UiHostPort &port,const desktop::DesktopApplication *builtins,size_t count)
      : port_(port),builtins_(builtins),builtin_count_(count),catalog_(builtins,count) {}
  bool bind(ApplicationsService &service) {
    if (runtime_ && runtime_ != &service) return false;
    if (!runtime_) { runtime_ = &service; downloaded_ = DownloadedSurface(application_foreground_ports(service)); }
    return true;
  }
  const PackageUiState &view() const { return ui_; }
  manager::RenderPresentation presentation() const { return {ui_.store_ui,ui_.store}; }
  DownloadedPresentation downloaded_presentation() const;
  static const char *service_id() { return kApplicationsServiceId; }
  ApplicationDataPorts data_ports();
  PackageManagementPorts management_ports();
  app::ApplicationStartResult open_backend(const std::string &,std::string &);
  bool close_backend(std::string &error) { return downloaded_.close(error); }
  bool open_store(bool refresh=false);
  template<class Open> bool open_downloaded(const std::string &id,Open open) {
    if(port_.ui_blocked())return false;
    std::string error;
    if(!preparation_.ready()){unavailable();return false;}
    update_catalog(true);
    if(!open(id,error)){log(error);return false;}
    port_.select_application(id.c_str(),true);downloaded_.refresh();ui_.app_frame=downloaded_.frame();
    port_.display_application(id);port_.rearm_ui();
    return port_.enter_ui({kDownloadedView});
  }
  bool update(const PackageDesktopInput &);
  void release_frame() { ui_.app_frame.reset(); }
  bool setup(KernelRuntime &, const char *version);
  bool ensure_available(std::string &);
  bool admit_desktop(const char *id,std::string &error) {
    if(id&&!std::strcmp(id,kDownloadedView.owner)){
      if(preparation_.ready() && runtime_ && runtime_->ready())return true;
      error="application_directory_unavailable";return false;
    }
    return id&&!std::strcmp(id,manager::kStoreView.owner)?ensure_available(error):true;
  }
  const app::ApplicationPreparation &preparation() const { return preparation_; }
  bool catalog_dirty() const { return catalog_.dirty(); }
  void poll(bool commands_allowed);
  void capture_view();
  void update_catalog(bool force = false);
  void action(uint32_t);
  manager::DesktopInteractionResult interact(const manager::DesktopInteractionInput &);
  void process_command();
  bool queue_command(const char *);
  bool cancel_input() { return downloaded_.cancel_input(); }
  const std::string &running_id() const { static const std::string empty; return runtime_ ? runtime_->running_id() : empty; }
 private:
  bool store_visible() const { return port_.ui_route()==manager::kStoreView; }
  void unavailable();
  void log(const std::string &);
  desktop::UiHostPort &port_;
  PackageUiState ui_;
  const desktop::DesktopApplication *builtins_;
  size_t builtin_count_;
  ApplicationsService *runtime_ = nullptr; // Borrowed from system composition.
  DownloadedSurface downloaded_{{}};
  Store store_;
  StoreConsole store_console_;
  RuntimeConsole runtime_console_;
  std::atomic<bool> command_pending_{false};
  std::mutex command_mutex_;
  std::string command_, version_;
  app::ApplicationPreparation preparation_;
  DesktopCatalogController catalog_;
  manager::DesktopInteraction interaction_;
};
} // namespace lunaredu::lunaros::apps
