#pragma once

#include <stddef.h>
#include <string>

#include "lunaredu_lunaros/kernel/service_host.h"

namespace lunaredu::lunaros {

inline constexpr size_t kProductDeclarationCapacity = 32;

// A declaration contributes static product metadata/configuration. It may
// register models, projections or bounded executors, but it must not start a
// service, initialize hardware or create a task. Runtime availability is
// changed later by the owning ServiceNode.
using ProductDeclarationInstaller = bool (*)(void *context,
                                             std::string &error);

struct ProductDeclarationNode {
  const char *id = nullptr;
  ProductDeclarationInstaller install = nullptr;
  void *context = nullptr;
};

using ProductDeclarationReset = void (*)(void *context);

// ProductDefinition is the only static composition input accepted by
// LunarOS. Products explicitly declare both their complete service graph and
// their non-running model/projection surface. Global constructors and hidden
// service-start registration are forbidden.
struct ProductDefinition {
  const char *id = nullptr;
  const ServiceNode *services = nullptr;
  size_t service_count = 0;
  const ProductDeclarationNode *declarations = nullptr;
  size_t declaration_count = 0;
  ProductDeclarationReset reset_declarations = nullptr;
  void *declaration_context = nullptr;
};

bool install_product_definition(const ProductDefinition &definition,
                                std::string &error);

} // namespace lunaredu::lunaros
