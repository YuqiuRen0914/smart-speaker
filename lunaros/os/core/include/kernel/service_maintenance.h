#pragma once
#include "kernel/runtime.h"
namespace lunaredu::lunaros {
// Core-owned coordination around an explicit service restart.
// The owner must outlive its registration and unregister before destruction.
struct ServiceMaintenanceCoordinator {
  void *context = nullptr;
  bool (*before_restart)(void *context, const char *service_id,
                         std::string &error) = nullptr;
  bool (*after_restart)(void *context, const char *service_id,
                        bool service_restarted, std::string &error) = nullptr;
};

class ServiceMaintenanceAccess {
public:
  static bool can_bind(const KernelRuntime &runtime,std::string &error) {return runtime.can_bind_service_maintenance(error);}
  static bool bind(KernelRuntime &runtime,ServiceMaintenanceCoordinator coordinator,std::string &error) {return runtime.bind_service_maintenance(coordinator,error);}
  static void unbind(KernelRuntime &runtime,void *context) {runtime.unbind_service_maintenance(context);}
};
}
