#pragma once
#include "app/lifecycle/hosted_application.h"
#include "app/package/application_data_ports.h"
#include "desktop/shell/shell.h"
#include <memory>
#include <new>

namespace lunaredu::desktop { class DesktopUiHost; }
namespace lunaredu::lunaros {
class KernelRuntime;
struct DesktopAppDescriptor;
struct DesktopApplicationPorts : app::HostedApplicationPorts {
  app::ApplicationStartResult (*open_guest)(void *, const std::string &, std::string &) = nullptr;
  bool (*close_guest)(void *, std::string &) = nullptr;
  const app::ApplicationPreparation *package_preparation = nullptr;
  const char *package_service_id = nullptr; // Borrowed provider identity, not a concrete Service dependency.
  desktop::DesktopShell *shell = nullptr;
  uint32_t (*now_ms)() = nullptr;
  apps::ApplicationDataPorts data;
  // Borrowed descriptors must outlive preparation and observer polling.
  const DesktopAppDescriptor *providers = nullptr;
  size_t provider_count = 0;
};

class DesktopAppInstance {
 public:
  virtual ~DesktopAppInstance() = default;
  virtual bool declare(app::AppController &,DesktopApplicationPorts,std::string &) = 0;
};
// Each App provides this adapter; only selected owners are created and retained.
template<class Application,class Registration>
class DesktopAppOwner final : public DesktopAppInstance {
 public:
  bool declare(app::AppController &controller,DesktopApplicationPorts ports,std::string &error) override {
    return Registration{}(application_,controller,ports,error);
  }
  static std::unique_ptr<DesktopAppInstance> create() {
    return std::unique_ptr<DesktopAppInstance>(new(std::nothrow) DesktopAppOwner);
  }
 private:
  Application application_;
};
struct DesktopAppDescriptor {
  const char *id;
  std::unique_ptr<DesktopAppInstance> (*create)();
  bool (*prepare)(std::string &) = nullptr;
  void (*configure)(KernelRuntime &) = nullptr;
  bool (*poll)(desktop::DesktopShell &,uint32_t,bool) = nullptr;
};

// Retained default composition binding. Declaration does not enter an App or
// initialize a VM. Product callbacks run before foreground projection.
class DesktopApplicationBindings {
public:
  explicit DesktopApplicationBindings(desktop::DesktopUiHost &ui) : ui_(ui) {}
  DesktopApplicationBindings(const DesktopApplicationBindings &) = delete;
  DesktopApplicationBindings &operator=(const DesktopApplicationBindings &) = delete;
  DesktopApplicationPorts ports(app::HostedApplicationPorts,
      uint32_t (*now_ms)() = nullptr);
  template<class Packages>
  void package_ports(DesktopApplicationPorts &result,Packages &packages) {
    package_context_=&packages;
    admit_package_=[](void *p,const char *id,std::string &e){return static_cast<Packages *>(p)->admit_desktop(id,e);};
    result.package_preparation=&packages.preparation();result.data=packages.data_ports();
    result.package_service_id=packages.service_id();
    bind_maintenance(result.data);
    result.open_guest=[](void *p,const std::string &id,std::string &e){
      return static_cast<Packages *>(static_cast<DesktopApplicationBindings *>(p)->package_context_)->open_backend(id,e);
    };
    result.close_guest=[](void *p,std::string &e){
      return static_cast<Packages *>(static_cast<DesktopApplicationBindings *>(p)->package_context_)->close_backend(e);
    };
  }
  bool enter(const char *, std::string &error);
  void bind_maintenance(apps::ApplicationDataPorts);
private:
  desktop::DesktopUiHost &ui_;
  app::HostedApplicationPorts product_;
  void *package_context_ = nullptr;
  bool (*admit_package_)(void *,const char *,std::string &) = nullptr;
};

// Desktop owns Shell/downloaded mechanisms and retains App-owned adapters
// supplied by the product. Only selected entries are registered.
// Product/native Apps are registered separately on the same AppController.
// Declaration neither launches an App nor starts a package runtime/VM.
class DesktopApplications {
public:
  DesktopApplications();
  ~DesktopApplications();
  DesktopApplications(const DesktopApplications &) = delete;
  DesktopApplications &operator=(const DesktopApplications &) = delete;
  bool declare(app::AppController &, const char *shell_id,
               const desktop::DesktopApplication *, size_t count,
               DesktopApplicationPorts, std::string &error);
  bool open_guest(const std::string &, std::string &error);
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
