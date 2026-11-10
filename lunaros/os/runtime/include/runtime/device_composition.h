#pragma once
#include "runtime/device_options.h"
#include <array>
#include <type_traits>

namespace lunaredu::lunaros {
// Retained metadata only. Modules pair service nodes, API declarations and
// asset requirements and owner-bound upkeep without starting hardware, tasks
// or applications.
template<size_t Services, size_t Declarations, size_t Assets = 0, size_t Maintenance = 0>
struct DeviceModule {
  std::array<ServiceNode, Services> services{};
  std::array<ProductDeclarationNode, Declarations> declarations{};
  std::array<ManagedAssetDefinition, Assets> assets{};
  std::array<ModuleMaintenance, Maintenance> maintenance{};
};
template<class... Declarations> auto device_module(ServiceNode service, Declarations... declarations) {
  static_assert((std::is_same_v<Declarations, ProductDeclarationNode> && ...));
  return DeviceModule<1, sizeof...(Declarations)>{{service}, {declarations...}, {}};
}
template<class... Declarations> auto device_projection_module(Declarations... declarations) {
  static_assert((std::is_same_v<Declarations, ProductDeclarationNode> && ...));
  return DeviceModule<0, sizeof...(Declarations)>{{}, {declarations...}, {}};
}
template<class... Assets> auto device_asset_module(Assets... assets) {
  static_assert((std::is_same_v<Assets, ManagedAssetDefinition> && ...));
  return DeviceModule<0, 0, sizeof...(Assets)>{{}, {}, {assets...}};
}
template<size_t Services, size_t Declarations, size_t Assets, size_t Maintenance = 0>
struct DeviceComposition : DeviceModule<Services, Declarations, Assets, Maintenance> {
  ProductDefinition definition(const char *id, ProductDeclarationReset reset = nullptr,
                               void *context = nullptr) const {
    static_assert(Assets == 0, "Executor hosts configure their assets separately");
    static_assert(Maintenance == 0, "Executor hosts configure their module maintenance separately");
    return {id, this->services.data(), Services, this->declarations.data(),
            Declarations, reset, context};
  }
  // The composition and borrowed contexts must outlive registration and execution.
  void apply(DeviceOptions &options) const {
    options.services = {this->services.data(), Services};
    options.declarations = {this->declarations.data(), Declarations};
    options.assets = {this->assets.data(), Assets};
    options.module_maintenance = {this->maintenance.data(), Maintenance};
  }
};
template<class... Modules> auto device_modules(const Modules &...modules) {
  DeviceComposition<(std::tuple_size_v<decltype(modules.services)> + ... + 0),
                    (std::tuple_size_v<decltype(modules.declarations)> + ... + 0),
                    (std::tuple_size_v<decltype(modules.assets)> + ... + 0),
                    (std::tuple_size_v<decltype(modules.maintenance)> + ... + 0)> result;
  if constexpr (sizeof...(Modules)) {
    size_t service = 0, declaration = 0, asset = 0, maintenance = 0;
    const auto append = [&](const auto &module) {
      for (const auto &node : module.services) result.services[service++] = node;
      for (const auto &node : module.declarations) result.declarations[declaration++] = node;
      for (const auto &node : module.assets) result.assets[asset++] = node;
      for (const auto &node : module.maintenance) result.maintenance[maintenance++] = node;
    };
    (append(modules), ...);
  }
  return result;
}
}
