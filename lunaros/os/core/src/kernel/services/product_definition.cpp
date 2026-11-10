#include "kernel/runtime_internal.h"
#include "kernel/product_definition.h"
#include "kernel/runtime.h"

#include <cstring>

#include "kernel/services/service_host_internal.h"
#include "kernel/services/service_registry_internal.h"
#include "kernel/tasks/task_registry_internal.h"

namespace lunaredu::lunaros {
namespace {

bool validate_definition(const ProductDefinition &definition,
                         std::string &error) {
  if (definition.id == nullptr || definition.id[0] == '\0') {
    error = "invalid_product_id";
    return false;
  }
  if (definition.services == nullptr || definition.service_count == 0) {
    error = "product_services_empty";
    return false;
  }
  if (definition.declaration_count == 0) {
    if (definition.declarations != nullptr) {
      error = "product_declaration_count_missing";
      return false;
    }
    return true;
  }
  if (definition.declarations == nullptr) {
    error = "product_declarations_missing";
    return false;
  }
  if (definition.reset_declarations == nullptr) {
    error = "product_declaration_reset_missing";
    return false;
  }
  for (size_t index = 0; index < definition.declaration_count; ++index) {
    const ProductDeclarationNode &node = definition.declarations[index];
    if (node.id == nullptr || node.id[0] == '\0' || node.install == nullptr) {
      error = "invalid_product_declaration:" + std::to_string(index);
      return false;
    }
    for (size_t previous = 0; previous < index; ++previous) {
      if (std::strcmp(definition.declarations[previous].id, node.id) == 0) {
        error = "duplicate_product_declaration:" + std::string(node.id);
        return false;
      }
    }
  }
  return true;
}

} // namespace

template <typename ResetComposition>
bool install_into(ServiceHost &host, ServiceRegistry &registry,
                  const ProductDefinition &definition, std::string &error,
                  ResetComposition reset_composition) {
  error.clear();
  if (!validate_definition(definition, error)) {
    return false;
  }

  // Validation precedes mutation. Withdraw the graph only after the declaration
  // owner acknowledges retirement; rejected cleanup retains it and both errors.
  if (!reset_composition(error)) {
    if (error.empty()) error = "product_composition_reset_failed";
    return false;
  }
  const auto rollback = [&] {
    std::string reset_error;
    if (!reset_composition(reset_error))
      error += "; reset: " + (reset_error.empty()
          ? std::string("product_composition_reset_failed") : reset_error);
  };
  for (size_t index = 0; index < definition.service_count; ++index) {
    if (!host.install(definition.services[index], error)) {
      error = "product_service_install_failed:" + std::to_string(index) +
              ":" + error;
      rollback();
      return false;
    }
  }
  if (!registry.validate_graph(error)) {
    error = "product_service_graph_invalid:" + error;
    rollback();
    return false;
  }
  for (size_t index = 0; index < definition.declaration_count; ++index) {
    const ProductDeclarationNode &node = definition.declarations[index];
    if (!node.install(node.context, error)) {
      error = "product_declaration_install_failed:" + std::string(node.id) +
              ":" + error;
      rollback();
      return false;
    }
  }
  return true;
}

bool KernelRuntime::install_product(const ProductDefinition &definition,
                                    std::string &error) {
  return install_product_definition(*this, definition, error);
}

bool install_product_definition(KernelRuntime &runtime,
                                const ProductDefinition &definition,
                                std::string &error) {
  ServiceHost &host = KernelRuntimeAccess::host(runtime);
  ServiceRegistry &registry = KernelRuntimeAccess::services(runtime);
  if (!registry.initialize(error)) {
    return false;
  }
  return install_into(host, registry, definition, error, [&](std::string &reset_error) {
    // Composition owns declarations, not the execution environment prepared
    // by setup_core_runtime. The owner may already be bound (maintenance),
    // but services and managed workers must not be running during installation.
    if (!runtime.can_reset_composition(reset_error)) {
      return false;
    }
    if (definition.reset_declarations &&
        !definition.reset_declarations(definition.declaration_context, reset_error)) return false;
    host.reset();
    registry.reset();
    return true;
  });
}

} // namespace lunaredu::lunaros
