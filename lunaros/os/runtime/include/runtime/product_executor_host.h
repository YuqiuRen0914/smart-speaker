#pragma once
namespace lunaredu::lunaros { struct ProvisioningPolicy; }
#include "lunaros_network_policy.h"
#include "runtime/product_runtime.h"
#include "kernel/executor_tasks.h"

namespace lunaredu::lunaros {
// Dedicated product executors use the same ordered bootstrap as ProductRuntime.
// Task budgets and tick callbacks remain explicit; this owner supplies the
// lifetime, startup binding and health query without product callback bridges.
class ProductExecutorHost : public ProductRuntime {
public:
  ProductExecutorHost() : tasks_(runtime()) {}
  ~ProductExecutorHost() { assert_owner_lifetime(); }
  using ServiceInstaller = bool (*)(ProductRuntime &, std::string &);
  bool start(std::initializer_list<ServiceExecutorDefinition> executors,
      const NetworkPolicy &network, const char *timezone,
      ServiceInstaller install, std::initializer_list<ExecutorTaskSpec> tasks,
      RuntimeBootstrapStage *failed_stage, std::string &error,
      const lunaredu::lunaros::ProvisioningPolicy *provisioning = nullptr,
      std::span<const ModuleMaintenance> maintenance = {},
      std::span<const RuntimeHealthProbe> health = {});

  bool executors_healthy(uint32_t maximum_silence_ms = 2000) const {
    return tasks_.healthy(maximum_silence_ms);
  }
  // Call outside the declared group, after stopping dependent services. New
  // foreign calls are rejected, accepted work drains before leave/unbind;
  // a pending stop retains the group and must be retried. Stops product tasks.
  // Shared I/O bindings and the ProductRuntime owner remain boot-lived;
  // successful task retirement does not permit destruction of this host.
  bool stop_executors(std::string &error) { return tasks_.stop(error); }
private:
  ExecutorTaskGroup tasks_;
};
}
