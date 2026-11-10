#include "lunaredu_lunaros/kernel/product_definition.h"

#include <cstring>

#include "kernel/services/service_host_internal.h"
#include "kernel/services/service_registry_internal.h"

namespace lunaredu::lunaros {
namespace {

void reset_composition(const ProductDefinition &definition) {
  reset_service_registry_internal();
  reset_service_host_internal();
  if (definition.reset_declarations != nullptr) {
    definition.reset_declarations(definition.declaration_context);
  }
}

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
  if (definition.service_count > kServiceCapacity) {
    error = "product_service_capacity_exceeded";
    return false;
  }
  if (definition.declaration_count > kProductDeclarationCapacity) {
    error = "product_declaration_capacity_exceeded";
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

bool install_product_definition(const ProductDefinition &definition,
                                std::string &error) {
  error.clear();
  if (!validate_definition(definition, error)) {
    return false;
  }

  // Product installation owns the complete graph. Clear both stores before
  // installing so a retry cannot inherit nodes from a failed or previous
  // definition. If validation fails, leave LunarOS in a clean, uncomposed
  // state rather than exposing a partially installed product.
  reset_composition(definition);
  ServiceHost &host = service_host_internal();
  for (size_t index = 0; index < definition.service_count; ++index) {
    if (!host.install(definition.services[index], error)) {
      error = "product_service_install_failed:" + std::to_string(index) +
              ":" + error;
      reset_composition(definition);
      return false;
    }
  }
  if (!service_registry_internal().validate_graph(error)) {
    error = "product_service_graph_invalid:" + error;
    reset_composition(definition);
    return false;
  }
  for (size_t index = 0; index < definition.declaration_count; ++index) {
    const ProductDeclarationNode &node = definition.declarations[index];
    if (!node.install(node.context, error)) {
      error = "product_declaration_install_failed:" + std::string(node.id) +
              ":" + error;
      reset_composition(definition);
      return false;
    }
  }
  return true;
}

} // namespace lunaredu::lunaros
