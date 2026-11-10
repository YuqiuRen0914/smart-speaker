#include <iostream>
#include <string>

#include "lunaredu_lunaros/kernel/product_definition.h"
#include "lunaredu_lunaros/kernel/service_registry.h"

namespace {

int g_failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __func__ << ":" << __LINE__                              \
                << " check failed: " #condition << std::endl;                \
      ++g_failures;                                                            \
    }                                                                          \
  } while (false)

using namespace lunaredu::lunaros;

ServiceStartResult ready(void *) {
  return {ServiceStartDisposition::Ready, "ready"};
}

int g_declaration_installs = 0;
int g_declaration_resets = 0;

bool install_declaration(void *, std::string &error) {
  ++g_declaration_installs;
  error.clear();
  return true;
}

bool fail_declaration(void *, std::string &error) {
  ++g_declaration_installs;
  error = "declaration_failed";
  return false;
}

void reset_declarations(void *) { ++g_declaration_resets; }

void test_installs_complete_explicit_graph() {
  constexpr ServiceDependency child_dependencies[] = {{"root", true}};
  const ServiceNode nodes[] = {
      {{"root", "Root", "main", ServiceCriticality::Required, nullptr, 0},
       ready},
      {{"child", "Child", "main", ServiceCriticality::Required,
        child_dependencies, 1},
       ready},
  };
  std::string error;
  const ProductDeclarationNode declarations[] = {
      {"model", install_declaration, nullptr},
  };
  g_declaration_installs = 0;
  g_declaration_resets = 0;
  CHECK(install_product_definition({"test-product", nodes, 2, declarations,
                                    1, reset_declarations, nullptr},
                                   error));
  CHECK(error.empty());
  CHECK(service_registry_snapshot().service_count == 2);
  CHECK(g_declaration_installs == 1);
  CHECK(g_declaration_resets == 1);
}

void test_rejects_invalid_product_without_replacing_graph() {
  std::string error;
  CHECK(!install_product_definition({nullptr, nullptr, 0}, error));
  CHECK(error == "invalid_product_id");
  CHECK(service_registry_snapshot().service_count == 2);
}

void test_failed_graph_leaves_clean_composition() {
  constexpr ServiceDependency missing_dependencies[] = {{"missing", true}};
  const ServiceNode nodes[] = {
      {{"blocked", "Blocked", "main", ServiceCriticality::Required,
        missing_dependencies, 1},
       ready},
  };
  std::string error;
  CHECK(!install_product_definition({"broken-product", nodes, 1}, error));
  CHECK(error.rfind("product_service_graph_invalid:", 0) == 0);
  CHECK(service_registry_snapshot().service_count == 0);
}

void test_failed_install_leaves_clean_composition() {
  const ServiceNode nodes[] = {
      {{"duplicate", "One", "main", ServiceCriticality::Required, nullptr,
        0},
       ready},
      {{"duplicate", "Two", "main", ServiceCriticality::Required, nullptr,
        0},
       ready},
  };
  std::string error;
  CHECK(!install_product_definition({"duplicate-product", nodes, 2}, error));
  CHECK(error.rfind("product_service_install_failed:1:", 0) == 0);
  CHECK(service_registry_snapshot().service_count == 0);
}

void test_failed_declaration_rolls_back_complete_composition() {
  const ServiceNode nodes[] = {
      {{"root", "Root", "main", ServiceCriticality::Required, nullptr, 0},
       ready},
  };
  const ProductDeclarationNode declarations[] = {
      {"broken", fail_declaration, nullptr},
  };
  g_declaration_installs = 0;
  g_declaration_resets = 0;
  std::string error;
  CHECK(!install_product_definition({"broken-declaration-product", nodes, 1,
                                     declarations, 1, reset_declarations,
                                     nullptr},
                                    error));
  CHECK(error ==
        "product_declaration_install_failed:broken:declaration_failed");
  CHECK(service_registry_snapshot().service_count == 0);
  CHECK(g_declaration_installs == 1);
  CHECK(g_declaration_resets == 2);
}

void test_rejects_invalid_declarations_before_mutation() {
  const ServiceNode nodes[] = {
      {{"root", "Root", "main", ServiceCriticality::Required, nullptr, 0},
       ready},
  };
  const ProductDeclarationNode declarations[] = {
      {"duplicate", install_declaration, nullptr},
      {"duplicate", install_declaration, nullptr},
  };
  const size_t existing_count = service_registry_snapshot().service_count;
  std::string error;
  CHECK(!install_product_definition({"invalid-declarations", nodes, 1,
                                     declarations, 2, reset_declarations,
                                     nullptr},
                                    error));
  CHECK(error == "duplicate_product_declaration:duplicate");
  CHECK(service_registry_snapshot().service_count == existing_count);
}

} // namespace

int main() {
  test_installs_complete_explicit_graph();
  test_rejects_invalid_product_without_replacing_graph();
  test_failed_graph_leaves_clean_composition();
  test_failed_install_leaves_clean_composition();
  test_failed_declaration_rolls_back_complete_composition();
  test_rejects_invalid_declarations_before_mutation();
  if (g_failures != 0) {
    std::cerr << g_failures << " ProductDefinition checks failed"
              << std::endl;
    return 1;
  }
  std::cout << "6 ProductDefinition composition checks passed" << std::endl;
  return 0;
}
