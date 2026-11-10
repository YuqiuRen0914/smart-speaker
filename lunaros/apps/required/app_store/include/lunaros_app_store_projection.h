#pragma once
#include "lunaros_package_management.h"
#include "device_api/device_operation_builders.h"
#include "device_api/device_projection.h"
#include <array>
namespace lunaredu::lunaros::apps {
// Optional product-retained adapter; every callback borrows the selected Store.
// Construct/declare before sealing, after selecting the desktop package session.
class PackageManagementProjection {
public:
  PackageManagementProjection();
  PackageManagementProjection(const PackageManagementProjection &)=delete;
  PackageManagementProjection &operator=(const PackageManagementProjection &)=delete;
  bool declare(DeviceProjectionRegistry &,PackageManagementPorts,std::string &);
private:
  DeviceOperationResult status(JsonObject,bool catalog);
  DeviceOperationResult command(unsigned,JsonVariantConst,JsonObject);
  PackageManagementPorts ports_;
  std::array<DeviceJsonBinding,6> bindings_;
  std::array<DeviceOperationDefinition,6> operations_;
  DeviceCapabilityDescriptor capability_;
  DeviceModuleProjectionMetadata metadata_;
  DeviceModuleProjection api_;
  DeviceProjectionRegistry *registry_=nullptr;
};
}
