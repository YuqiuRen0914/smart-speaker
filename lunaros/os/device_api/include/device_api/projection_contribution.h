#pragma once
#include <string>
#include "kernel/product_definition.h"
namespace lunaredu::lunaros {
class DeviceProjectionRegistry;
// Pure declaration recipes; creation may only bind a borrowed registry.
// The product chooses this range independently of application factories.
using DeviceProjectionDeclarationFactory = ProductDeclarationNode (*)(DeviceProjectionRegistry &);
struct DeviceProjectionSelection {
  const DeviceProjectionDeclarationFactory *data = nullptr;
  size_t size = 0;
};
struct StandardProjectionContribution {
  const char *id;
  bool (*install)(DeviceProjectionRegistry &, std::string &error);
};
}
