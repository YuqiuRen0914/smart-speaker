#include "composition/runtime_projections.h"
#include "device_api/device_operation_builders.h"
#include "device_api/projection_composition.h"
#include "system/system_projection.h"
#ifdef LUNAROS_DIAGNOSTICS_COMPONENT
#include "lunaros_diagnostics_projection.h"
#endif
namespace lunaredu::lunaros {
namespace {
void fill_composed_system_capabilities(DeviceApiContext &device, JsonObject payload) {
 fill_system_base_capabilities(device,payload);
#ifdef LUNAROS_DIAGNOSTICS_COMPONENT
 fill_diagnostics_capabilities(device,payload);
#endif
}
constexpr auto capabilities_operation=device_operation_on_caller(
 make_device_json_provider("capabilities.get",fill_composed_system_capabilities));
DeviceOperationsView additional_operations(){
#ifdef LUNAROS_DIAGNOSTICS_COMPONENT
 return diagnostics_device_operations();
#else
 return {};
#endif
}
bool install_system_projection(DeviceProjectionRegistry &registry,std::string &error){
 const auto &base=system_base_projection();
 static const DeviceOperationsView ranges[] = {
   {&capabilities_operation, 1}, additional_operations()};
 static const DeviceModuleProjection projection{base.module->capability_id,base.module->module,
   DeviceOperationSequence{system_base_operations(),ranges,2}};
 return registry.register_projection({base.capability,&projection,base.metadata,true},&error);
}
bool install_debug_projection(DeviceProjectionRegistry &registry,std::string &error){
#ifdef LUNAROS_DIAGNOSTICS_COMPONENT
 return install_diagnostics_debug_projection(registry,error);
#else
 (void)registry;error.clear();return true;
#endif
}
}
const StandardProjectionContribution standard_projection::system{"system",install_system_projection};
const StandardProjectionContribution standard_projection::debug{"debug",install_debug_projection};
}
