#pragma once
#include <vector>
#include "device_api/projection_contribution.h"
#include "kernel/product_definition.h"
#include "runtime/execution_contribution.h"

namespace lunaredu::app { class AppController; }
namespace lunaredu::lunaros {
namespace apps { class ApplicationsService; }
class DeviceProjectionRegistry;
struct DeviceContext;
struct RuntimePreset;
struct RuntimeExecutionDefaults {
  std::vector<std::unique_ptr<RuntimeExecutionOwner>> &owners;
  RuntimeExecutionPorts ports;
};
// Device-only default owner retention. Advanced roots supply their own nodes.
bool install_product_composition(DeviceContext &,DeviceProjectionRegistry &,apps::ApplicationsService &,
    const ProductDefinition &,std::string &,const RuntimePreset &,RuntimeExecutionDefaults *);
bool validate_projection_selection(DeviceProjectionSelection, std::string &);
bool append_projection_declarations(DeviceProjectionRegistry &, DeviceProjectionSelection,
                                   std::vector<ProductDeclarationNode> &, std::string &);
} // namespace lunaredu::lunaros
