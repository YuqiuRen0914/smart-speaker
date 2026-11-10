#pragma once

#include <stddef.h>
#include <string>

#include "kernel/service.h"

namespace lunaredu::lunaros {

class KernelRuntime;

// A declaration contributes static product metadata/configuration. It may
// register models and projections, but it must not configure executors, bind
// application contexts, start services, initialize hardware or create tasks.
// Execution is prepared before installation and survives declaration rollback.
// Runtime availability is
// changed later by the owning ServiceNode.
using ProductDeclarationInstaller = bool (*)(void *context,
                                             std::string &error);

struct ProductDeclarationNode {
  const char *id = nullptr;
  ProductDeclarationInstaller install = nullptr;
  void *context = nullptr;
};

// Refuses unsafe lifecycle reset before the service graph is discarded.
using ProductDeclarationReset = bool (*)(void *context, std::string &error);

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

bool install_product_definition(KernelRuntime &runtime,
                                const ProductDefinition &definition,
                                std::string &error);

} // namespace lunaredu::lunaros
