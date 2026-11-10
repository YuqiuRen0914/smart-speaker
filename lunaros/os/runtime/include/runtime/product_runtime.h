#pragma once
namespace lunaredu::lunaros { struct ProvisioningPolicy; }
#include "runtime/device_context.h"
#include "runtime/preset.h"
#include "runtime/device_state.h"
#include "device_api/device_projection.h"
#include "kernel/runtime.h"
#include "lunaros_applications.h"
#include "lunaros_network_policy.h"
#include "runtime/module_maintenance.h"
#include <span>
#include <memory>
#include <cstdlib>
namespace lunaredu::lunaros {
class DeviceProjectionRegistry;
// Framework-owned blocking I/O executor, with a Runtime subsystem owner.
// Its boot-lifetime activation bridge
// admits the shared worker on demand; products must not create a second owner.
ServiceExecutorDefinition maintenance_io_executor(uint8_t queue_depth = 2);
enum class RuntimeBootstrapStage : uint8_t {
  CoreRuntime,
  Configuration,
  Network,
  ServiceRegistration,
  DeviceProjections,
  ExecutorTasks,
  Services,
};

struct ProductRuntimeHooks {
  const RuntimePreset *preset = nullptr;
  // Borrowed until synchronous start returns; nullptr selects service defaults.
  const lunaredu::lunaros::ProvisioningPolicy *provisioning = nullptr;
  bool (*register_services)(void *context, std::string &error) = nullptr;
  // Start only product main/network/hardware tasks; framework owns netio.
  bool (*start_executor_tasks)(void *context, std::string &error) = nullptr;
  void *context = nullptr;
  bool bind_main_and_network_to_caller = false;
  uint32_t boot_healthy_period_ms = 3000;
  std::span<const RuntimeHealthProbe> health{};
  void (*startup_observer)(void *,const char *,const char *,const char *) = nullptr;
  void *startup_observer_context = nullptr;
  // Boot-lived contributions, configured after registration and before tasks.
  std::span<const ModuleMaintenance> module_maintenance{};
};

// Existing foreground owner; no task, protocol policy or App side effects.
void poll_product_radio(DeviceContext &device);

// Call on the declared network executor. This state belongs to that owner;
// the OS polls transport and optional platform upkeep without another task.
struct NetworkServicePollState { uint32_t last_platform_poll_ms = 0; bool platform_polled = false; };
void poll_network_services(DeviceContext &, NetworkServicePollState &,
                           uint32_t platform_period_ms = 50);

// Advanced products with dedicated executor tasks use the same single-owner
// model as Device while retaining control of their task plan. This object must
// live for the whole boot.
class ModuleMaintenanceHost;
class ProductRuntime {
public:
  ProductRuntime();
  virtual ~ProductRuntime();
  ProductRuntime(const ProductRuntime &) = delete;
  ProductRuntime &operator=(const ProductRuntime &) = delete;

  bool start(std::initializer_list<ServiceExecutorDefinition> executors,
             const NetworkPolicy &network_policy, const char *timezone,
             ProductRuntimeHooks hooks, RuntimeBootstrapStage *failed_stage,
             std::string &error);
  // Main-only upkeep, including failed-boot cleanup and 20 ms service/boot
  // health sampling. Short application failures interrupt health immediately.
  // This includes one ApplicationsService poll; callers do not
  // separately poll that service. Network/I/O keep their declared owners.
  void poll(bool application_ready = true);
  // Other declared executors call this on their own loop. Main's poll already
  // includes it. A service which has stopped is never revived by this method.
  void poll_modules();
  apps::ApplicationsService &applications_service() { return applications_; }
  KernelRuntime &runtime() { return runtime_; }
  DeviceContext &context() { return device_; }
  DeviceProjectionRegistry &projections() { return projections_; }

protected:
  // Every derived start entry consumes the same one-shot admission, including
  // argument rejection before bootstrap has any side effects.
  bool admit_start(std::string &error);
  // Check before derived members disappear as well as before the runtime.
  void assert_owner_lifetime() const { if (worker_admitted_) std::abort(); }
private:
  bool attempted_ = false, worker_admitted_ = false, upkeep_polled_ = false;
  uint32_t last_upkeep_poll_ms_ = 0;
  std::string lifecycle_detail_;
  KernelRuntime runtime_;
  DeviceContext device_;
  RuntimeDeviceState state_;
  DeviceProjectionRegistry projections_;
  apps::ApplicationsService applications_;
  std::unique_ptr<ModuleMaintenanceHost> module_maintenance_;
};

const char *runtime_bootstrap_stage_name(RuntimeBootstrapStage stage);
const char *runtime_bootstrap_failure_reason(RuntimeBootstrapStage stage);

} // namespace lunaredu::lunaros
