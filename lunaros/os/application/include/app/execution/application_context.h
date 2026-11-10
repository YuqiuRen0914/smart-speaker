#pragma once
#include "kernel/execution_context.h"
#include "app/execution/application_maintenance.h"
namespace lunaredu::app { class AppController; }
namespace lunaredu::lunaros {
class ApplicationMaintenanceBinding;
// Runtime shares this execution/model base with the API view; a standalone
// Application host owns its own base and controller binding.
struct ApplicationContext : virtual ExecutionContext {
  explicit ApplicationContext(KernelRuntime &execution) : ExecutionContext(execution) {}
  ApplicationContext(const ApplicationContext &) = delete;
  ApplicationContext &operator=(const ApplicationContext &) = delete;
  app::AppController *application() const { return application_; }
  const apps::ApplicationMaintenancePorts *application_maintenance_ports() const { return maintenance_; }
private:
  friend class app::AppController;
  friend class ApplicationMaintenanceBinding;
  bool bind_maintenance(const apps::ApplicationMaintenancePorts &ports) {
    if (maintenance_ && maintenance_ != &ports) return false;
    maintenance_ = &ports; return true;
  }
  void unbind_maintenance(const apps::ApplicationMaintenancePorts &ports) {
    if (maintenance_ == &ports) maintenance_ = nullptr;
  }
  app::AppController *application_ = nullptr;
  const apps::ApplicationMaintenancePorts *maintenance_ = nullptr;
};
// Composition owns the binding, Application owns the contract. Stop readers
// before withdrawal; both context and ports outlive this allocation-free guard.
class ApplicationMaintenanceBinding {
public:
  ApplicationMaintenanceBinding(ApplicationContext &context,
      const apps::ApplicationMaintenancePorts &ports) : context_(context), ports_(ports) {
    // Even the same ports cannot have two independently retiring bindings.
    if (!context_.maintenance_) bound_ = context_.bind_maintenance(ports_);
  }
  ~ApplicationMaintenanceBinding() { if (bound_) context_.unbind_maintenance(ports_); }
  ApplicationMaintenanceBinding(const ApplicationMaintenanceBinding &) = delete;
  ApplicationMaintenanceBinding &operator=(const ApplicationMaintenanceBinding &) = delete;
  bool owns_context() const { return bound_; }
private:
  ApplicationContext &context_;
  const apps::ApplicationMaintenancePorts &ports_;
  bool bound_ = false;
};
}
