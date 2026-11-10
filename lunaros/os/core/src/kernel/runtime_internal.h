#pragma once
#include "kernel/runtime.h"
namespace lunaredu::lunaros {
class KernelRuntimeAccess {
public:
  static ServiceRegistry &services(KernelRuntime &r) { return r.service_registry(); }
  static ServiceHost &host(KernelRuntime &r) { return r.service_host(); }
  static ManagedTaskRegistry &tasks(KernelRuntime &r) { return r.task_registry(); }
};
}
