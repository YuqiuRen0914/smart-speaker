#include "adapters/execution_adapters.h"
#ifdef LUNAROS_PYTHON_SERVICE
#include "lunaros_micropython_lifecycle.h"
#include "lunaros_micropython_app_backend.h"
#include "runtime/device_context.h"
#include "lunaros_storage_service_id.h"
#include "lunaros_device_api_service_id.h"
#include <sdkconfig.h>
#include <new>
namespace lunaredu::lunaros {
namespace {
#if CONFIG_LUNAROS_ENABLE_MICROPYTHON
class PythonExecutionOwner final : public RuntimeExecutionOwner {
  DeviceContext &device_;
  micropython::RuntimeConfig config_;
public:
  PythonExecutionOwner(DeviceContext &device,RuntimeExecutionPorts ports) : device_(device) {
    config_.start_on_demand=true;
    config_.foreground_context=ports.foreground_context;
    config_.foreground_transition=ports.foreground_transition;
  }
  ServiceNode node() override {
    static const ServiceDependency dependencies[]={{kStorageServiceId,true},{kDeviceApiServiceId,true}};
    return micropython::runtime_lifecycle_node(device_,config_,dependencies,std::size(dependencies));
  }
};
#endif
}
constinit const RuntimeExecutionContribution kPythonExecutionContribution{
    .id="python",.service_id=micropython::kRuntimeServiceId,
    .configure_backend=[](std::string &error) {
      if(!micropython::declare_application_backend()){error="application_backend_registration_failed";return false;}
      error.clear();return true;
    },
#if CONFIG_LUNAROS_ENABLE_MICROPYTHON
    .create_default=[](DeviceContext &device,RuntimeExecutionPorts ports) {
      return std::unique_ptr<RuntimeExecutionOwner>(new(std::nothrow) PythonExecutionOwner(device,ports));
    },
    .executor={lunaredu::micropython::kRuntimeExecutorId,micropython::kRuntimeServiceId,
      static_cast<uint8_t>(CONFIG_LUNAREDU_MICROPYTHON_COMMAND_QUEUE_DEPTH)}
#endif
  };
}
#endif
