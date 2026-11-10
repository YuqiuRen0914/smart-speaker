#include "kernel/service.h"
#include "lunaros_factory_lifecycle.h"
#include "lunaros_factory.h"
#include "device_api/device_commands.h"
#include "kernel/service_executor.h"
#include "projection_lifecycle.h"
namespace lunaredu::lunaros {
const DeviceCommandExtension &factory_device_commands();
namespace {
bool (*projection_lifecycle)(bool)=nullptr;
ServiceStartResult start(void *) {
  if(!install_device_command_extension(factory_device_commands()))return {ServiceStartDisposition::Failed,"factory_usb_extension_conflict"};
  if(projection_lifecycle&&!projection_lifecycle(true)) {
    remove_device_command_extension(factory_device_commands());
    return {ServiceStartDisposition::Failed,"factory_projection_unavailable"};
  }
  // Contributions are complete before services start. Products without checks
  // still receive the same usable framework; freezing metadata runs no check.
  local_factory_mode().seal();
  local_factory_mode().set_available(true);
  return {ServiceStartDisposition::Ready,"factory_ready"};
}
bool stop(void *,std::string &error) {
  local_factory_mode().set_available(false);
  if(projection_lifecycle)projection_lifecycle(false);
  remove_device_command_extension(factory_device_commands());
  error.clear();return true;
}
}
void bind_factory_projection_lifecycle(bool (*callback)(bool)){projection_lifecycle=callback;}
ServiceNode factory_service_node() {
  static constexpr ServiceDependency dependencies[]={{kKernelReadyServiceId,true}};
  return {{kFactoryServiceId,"Factory checks",kMainServiceExecutor,ServiceCriticality::Required,dependencies,1},start,stop,nullptr};
}
}
