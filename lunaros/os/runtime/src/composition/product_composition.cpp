#include "composition/product_composition.h"
#include "runtime/product_composition.h"
#include "runtime/preset.h"
#include "startup/product_runtime_internal.h"
#include "device_api/projection_composition.h"
#include "lunaros_applications.h"
#include "lunaros_applications_service_id.h"
#include <algorithm>
#include <cstring>
#include <vector>
namespace lunaredu::lunaros {
bool validate_projection_selection(DeviceProjectionSelection selection, std::string &error) {
  error.clear();
  if ((selection.size && !selection.data)) {
    error = "invalid_device_projection_selection"; return false;
  }
  for (size_t i = 0; i < selection.size; ++i) {
    if (!selection.data[i]) { error = "invalid_device_projection_factory"; return false; }
    for (size_t j = 0; j < i; ++j) if (selection.data[i] == selection.data[j]) {
      error = "duplicate_device_projection_factory"; return false;
    }
  }
  return true;
}
bool append_projection_declarations(DeviceProjectionRegistry &registry, DeviceProjectionSelection selection,
    std::vector<ProductDeclarationNode> &declarations, std::string &error) {
  if (!validate_projection_selection(selection, error)) return false;
  for (size_t i = 0; i < selection.size; ++i) {
    const auto node = selection.data[i](registry);
    if (!node.id || !*node.id || !node.install) {
      error = "invalid_device_projection_declaration"; return false;
    }
    const bool supplied = std::any_of(declarations.begin(), declarations.end(), [&](const auto &existing) {
      return existing.id && !std::strcmp(existing.id, node.id);
    });
    if (!supplied) declarations.push_back(node);
  }
  return true;
}
bool install_product_composition(DeviceContext &device, DeviceProjectionRegistry &projections,
                                 apps::ApplicationsService &applications,
                                 const ProductDefinition &product,
                                 std::string &error, const RuntimePreset &preset,RuntimeExecutionDefaults *defaults) {
  error.clear();
  if (!validate_runtime_preset(preset,error)) return false;
  if (&projections.device() != static_cast<DeviceApiContext *>(&device) || !projections.owns_device()) {
    error = "runtime_projection_owner_mismatch"; return false;
  }
  if ((product.service_count && !product.services) ||
      (product.declaration_count && !product.declarations)) {
    error = "invalid_product_composition"; return false;
  }
  if (!product.id || !*product.id) { error="invalid_product_id"; return false; }
  // Validate metadata before creating any stateful default service node.
  for (size_t i=0;i<product.service_count;++i) {
    const auto *id=product.services[i].definition.id;
    if (!id || !*id) { error="invalid_product_service"; return false; }
    if (!std::strcmp(id,kApplicationsServiceId)) {
      error="applications_service_owned_by_composition"; return false;
    }
    for(size_t previous=0;previous<i;++previous)
      if(!std::strcmp(product.services[previous].definition.id,id)) {
        error="duplicate_product_service:"+std::string(id);return false;
      }
  }
  for(size_t i=0;i<product.declaration_count;++i) {
    const auto &node=product.declarations[i];
    if(!node.id || !*node.id || !node.install) {
      error="invalid_product_declaration:"+std::to_string(i);return false;
    }
    if(!std::strcmp(node.id,"lunaros.defaults")) {
      error="duplicate_product_declaration:"+std::string(node.id);return false;
    }
    for(size_t previous=0;previous<i;++previous)
      if(!std::strcmp(product.declarations[previous].id,node.id)) {
        error="duplicate_product_declaration:"+std::string(node.id);return false;
      }
  }
  auto &runtime=projections.runtime();
  if(!runtime.can_reset_composition(error) || !projections.context().control.can_reset(error)) return false;
  const auto &maintenance=apps::application_maintenance(device);
  if(maintenance.context && maintenance.context!=&applications) {
    error="applications_maintenance_owner_conflict";return false;
  }
  struct DeclarationRollback {
    KernelRuntime &runtime;apps::ApplicationsService &applications;std::string &error;
    bool installed=false;
    ~DeclarationRollback() {
      if(installed || runtime.service_registered(kApplicationsServiceId) || !applications.owned_by(runtime)) return;
      std::string detail;
      if(!applications.discard_uninstalled_declaration(detail)) error+=";declaration_cleanup_failed:"+detail;
    }
  } rollback{runtime,applications,error};
  // Only providers present in this graph contribute retirement edges. Service
  // names and VM-specific configuration belong to their adapter/preset.
  std::vector<ServiceNode> services;
  services.reserve(preset.service_count + product.service_count + 1);
  for (size_t i=0; i<preset.service_count; ++i) services.push_back(preset.services[i](device,projections));
  for (size_t i = 0; i < product.service_count; ++i) {
    const auto &node = product.services[i];
    const auto existing = std::find_if(services.begin(), services.end(), [&](const ServiceNode &value) {
      return value.definition.id && std::strcmp(value.definition.id, node.definition.id) == 0;
    });
    if(existing!=services.end() && existing->definition.criticality==ServiceCriticality::Required &&
       node.definition.criticality!=ServiceCriticality::Required) {
      error="required_service_cannot_be_downgraded:"+std::string(node.definition.id);return false;
    }
    if (existing == services.end()) services.push_back(node);
    else *existing = node;
  }
  if(defaults && preset.default_execution_services)for(const auto *provider:preset.execution) {
    if(!provider->create_default)continue;
    const bool supplied=std::any_of(services.begin(),services.end(),[&](const ServiceNode &node) {
      return node.definition.id && !std::strcmp(node.definition.id,provider->service_id);
    });
    if(supplied)continue;
    auto owner=provider->create_default(device,defaults->ports);
    if(!owner){error="runtime_execution_owner_allocation_failed:"+std::string(provider->id);return false;}
    auto node=owner->node();
    if(!node.definition.id || std::strcmp(node.definition.id,provider->service_id)) {
      error="runtime_execution_service_mismatch:"+std::string(provider->id);return false;
    }
    defaults->owners.push_back(std::move(owner));
    services.push_back(node);
  }
  std::vector<ServiceDependency> engines;
  for(const auto *provider:preset.execution)if(provider->service_id)
    for(const auto &node:services)
      if(node.definition.id && !std::strcmp(node.definition.id,provider->service_id)) {
        engines.push_back({provider->service_id,true});break;
      }
  services.insert(services.begin(),applications.node(runtime,engines.data(),engines.size()));
  struct Reset {
    DeviceContext &device;
    DeviceProjectionRegistry &projections;
    apps::ApplicationsService &applications;
    const ProductDefinition &product;
    const RuntimePreset &preset;
  } reset{device, projections, applications, product, preset};
  // These callbacks run synchronously; only the projections/service owner
  // escapes into the graph. Rollback withdraws the borrowed maintenance port.
  std::vector<ProductDeclarationNode> declarations{{"lunaros.defaults",
      [](void *opaque,std::string &failure) {
        auto &value=*static_cast<Reset *>(opaque);
        if(value.preset.projection_count && !install_standard_device_projections(value.preset.projections,value.preset.projection_count,value.projections,failure))return false;
        if(!ProductRuntimeAccess::bind_application_maintenance(value.device,
            apps::application_maintenance_ports(value.applications))) {
          failure="applications_maintenance_owner_conflict";return false;
        }
        return true;
      },&reset}};
  declarations.reserve(product.declaration_count+1);
  for (size_t i = 0; i < product.declaration_count; ++i)
    declarations.push_back(product.declarations[i]);
  rollback.installed = runtime.install_product(
      {product.id, services.data(), services.size(), declarations.data(), declarations.size(),
       [](void *opaque, std::string &reset_error) {
         auto &value = *static_cast<Reset *>(opaque);
         if (!value.projections.context().control.can_reset(reset_error)) return false;
         if (value.product.reset_declarations &&
             !value.product.reset_declarations(value.product.declaration_context, reset_error)) return false;
         if(!ProductRuntimeAccess::reset(value.projections, reset_error))return false;
         ProductRuntimeAccess::clear_application_maintenance(value.device,&value.applications);
         return true;
       }, &reset}, error);
  return rollback.installed;
}
bool install_product_composition(DeviceContext &device, DeviceProjectionRegistry &projections,
    apps::ApplicationsService &applications,const ProductDefinition &product,std::string &error,const RuntimePreset &preset) {
  return install_product_composition(device,projections,applications,product,error,preset,nullptr);
}
bool install_product_composition(DeviceContext &device, DeviceProjectionRegistry &projections,
    apps::ApplicationsService &applications, const ProductDefinition &product, std::string &error) {
  return install_product_composition(device,projections,applications,product,error,default_runtime_preset());
}
} // namespace lunaredu::lunaros
