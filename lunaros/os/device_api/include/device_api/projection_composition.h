#pragma once

#include <array>
#include "device_api/projection_contribution.h"
#include "kernel/product_definition.h"

namespace lunaredu::lunaros {

class DeviceProjectionRegistry;

// A retained registry is the only context required by a projection provider.
// An explicitly omitted contribution installs nothing and starts nothing.
template<bool (*Install)(DeviceProjectionRegistry &,std::string &)>
ProductDeclarationNode projection_declaration_node(const char *id,DeviceProjectionRegistry &registry,bool selected=true) {
  return {id,[](void *context,std::string &error){
    return !context||Install(*static_cast<DeviceProjectionRegistry *>(context),error);
  },selected?&registry:nullptr};
}
inline ProductDeclarationNode product_declaration_node(const char *id,ProductDeclarationInstaller install,
    void *context,bool selected=true) {
  constexpr ProductDeclarationInstaller omitted=[](void *,std::string &){return true;};
  return {id,selected?install:omitted,context};
}

// Explicit static contributions: an unselected installer has no link reference.

bool install_standard_device_projections(const StandardProjectionContribution *const *contributions,
                                         size_t count, DeviceProjectionRegistry &registry,
                                         std::string &error);

template <const StandardProjectionContribution &... Contributions>
bool setup_standard_device_projections(DeviceProjectionRegistry &registry,
                                       std::string &error) {
  static constexpr std::array<const StandardProjectionContribution *, sizeof...(Contributions)> entries{&Contributions...};
  return install_standard_device_projections(entries.data(), entries.size(), registry, error);
}

template<const StandardProjectionContribution &... Contributions>
ProductDeclarationNode standard_projection_declaration_node(const char *id,DeviceProjectionRegistry &registry) {
  return projection_declaration_node<setup_standard_device_projections<Contributions...>>(id,registry);
}

} // namespace lunaredu::lunaros
