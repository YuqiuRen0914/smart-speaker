#include "composition/runtime_projections.h"
#include "device_api/projection_composition.h"
#include "lunaros_storage_projection.h"
#include "lunaros_resource_projection.h"
namespace lunaredu::lunaros {
namespace {
bool install_storage_projection(DeviceProjectionRegistry &registry,std::string &error){
  const auto &base=storage_device_projection();
  // Immutable, borrowed module descriptors must outlive every registry.
  static const DeviceOperationsView ranges[] = {storage_device_operations()};
  static const DeviceModuleProjection projection{base.module->capability_id,base.module->module,
    DeviceOperationSequence{resource_device_operations(), ranges, 1}};
  return registry.register_projection({base.capability,&projection,base.metadata,base.capability_active},&error);
}
}
const StandardProjectionContribution standard_projection::storage{"storage",install_storage_projection};
}
