#include <iostream>
#include <string>

#include "kernel/services/service_registry_internal.h"

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

void register_or_fail(lunaredu::lunaros::ServiceRegistry &registry,
                      const lunaredu::lunaros::ServiceDefinition &service) {
  std::string error;
  CHECK(registry.register_service(service, error));
  CHECK(error.empty());
}

void start_and_ready(lunaredu::lunaros::ServiceRegistry &registry,
                     const char *service_id) {
  std::string error;
  CHECK(registry.begin_start(service_id, error));
  CHECK(registry.mark_ready(service_id, "ready", error));
}

void test_dependency_order_and_required_readiness() {
  lunaredu::lunaros::ServiceRegistry registry;
  std::string initialization_error;
  CHECK(registry.initialize(initialization_error));
  CHECK(initialization_error.empty());
  lunaredu::lunaros::ServiceDefinition system;
  system.id = "system.runtime";
  system.criticality = lunaredu::lunaros::ServiceCriticality::Required;
  register_or_fail(registry, system);
  CHECK(registry.initialize(initialization_error));
  CHECK(initialization_error.empty());
  CHECK(registry.service_registered("system.runtime"));

  constexpr lunaredu::lunaros::ServiceDependency network_dependencies[] = {
      {"system.runtime", true},
  };
  lunaredu::lunaros::ServiceDefinition network;
  network.id = "network.runtime";
  network.owner_executor = "network";
  network.criticality = lunaredu::lunaros::ServiceCriticality::Optional;
  network.dependencies = network_dependencies;
  network.dependency_count = 1;
  register_or_fail(registry, network);

  std::string error;
  CHECK(!registry.begin_start("network.runtime", error));
  CHECK(error == "service_dependency_unavailable:system.runtime");
  CHECK(!registry.required_services_ready(&error));
  CHECK(error == "required_service_unavailable:system.runtime");

  start_and_ready(registry, "system.runtime");
  CHECK(registry.required_services_ready(&error));
  CHECK(registry.begin_start("network.runtime", error));
  CHECK(registry.mark_degraded("network.runtime", "offline", error));
  CHECK(registry.service_available("network.runtime"));
}

void test_stop_rejects_active_required_dependent() {
  lunaredu::lunaros::ServiceRegistry registry;
  std::string initialization_error;
  CHECK(registry.initialize(initialization_error));
  CHECK(initialization_error.empty());
  lunaredu::lunaros::ServiceDefinition system;
  system.id = "system.runtime";
  register_or_fail(registry, system);
  start_and_ready(registry, "system.runtime");

  constexpr lunaredu::lunaros::ServiceDependency dependencies[] = {
      {"system.runtime", true},
  };
  lunaredu::lunaros::ServiceDefinition micropython;
  micropython.id = "runtime.micropython";
  micropython.dependencies = dependencies;
  micropython.dependency_count = 1;
  register_or_fail(registry, micropython);
  start_and_ready(registry, "runtime.micropython");

  std::string error;
  CHECK(!registry.begin_stop("system.runtime", error));
  CHECK(error == "service_has_active_dependent:runtime.micropython");
  CHECK(registry.begin_stop("runtime.micropython", error));
  CHECK(registry.mark_stopped("runtime.micropython", "shutdown", error));
  CHECK(registry.begin_stop("system.runtime", error));
}

void test_failure_recovery_and_snapshot() {
  lunaredu::lunaros::ServiceRegistry registry;
  std::string initialization_error;
  CHECK(registry.initialize(initialization_error));
  CHECK(initialization_error.empty());
  CHECK(sizeof(lunaredu::lunaros::ServiceRegistrySnapshot) <= 4 * 1024);
  lunaredu::lunaros::ServiceDefinition assistant;
  assistant.id = "assistant.xiaozhi";
  assistant.title = "XiaoZhi";
  assistant.owner_executor = "network";
  register_or_fail(registry, assistant);

  std::string error;
  CHECK(registry.begin_start("assistant.xiaozhi", error));
  CHECK(registry.mark_failed("assistant.xiaozhi", "audio_init_failed",
                             error));
  CHECK(!registry.service_available("assistant.xiaozhi"));
  CHECK(registry.begin_start("assistant.xiaozhi", error));
  CHECK(registry.mark_ready("assistant.xiaozhi", "connected", error));

  const auto snapshot = registry.snapshot();
  CHECK(snapshot.service_count == 1);
  CHECK(std::string(snapshot.services[0].id) == "assistant.xiaozhi");
  CHECK(snapshot.services[0].state ==
        lunaredu::lunaros::ServiceState::Ready);
  CHECK(snapshot.services[0].detail == "connected");
  CHECK(snapshot.revision >= 5);
}

void test_registration_validation() {
  lunaredu::lunaros::ServiceRegistry registry;
  std::string initialization_error;
  CHECK(registry.initialize(initialization_error));
  CHECK(initialization_error.empty());
  constexpr lunaredu::lunaros::ServiceDependency self_dependency[] = {
      {"bad", true},
  };
  lunaredu::lunaros::ServiceDefinition invalid;
  invalid.id = "bad";
  invalid.dependencies = self_dependency;
  invalid.dependency_count = 1;
  std::string error;
  CHECK(!registry.register_service(invalid, error));
  CHECK(error == "invalid_service_dependency");
  CHECK(!registry.service_registered("bad"));
}

void test_graph_rejects_missing_required_dependency_and_cycle() {
  using namespace lunaredu::lunaros;
  std::string error;

  ServiceRegistry missing_registry;
  CHECK(missing_registry.initialize(error));
  CHECK(error.empty());
  constexpr ServiceDependency missing_dependency[] = {
      {"missing.service", true},
  };
  ServiceDefinition dependent;
  dependent.id = "dependent";
  dependent.dependencies = missing_dependency;
  dependent.dependency_count = 1;
  register_or_fail(missing_registry, dependent);
  CHECK(!missing_registry.validate_graph(error));
  CHECK(error ==
        "service_dependency_not_registered:dependent:missing.service");

  ServiceRegistry cycle_registry;
  CHECK(cycle_registry.initialize(error));
  CHECK(error.empty());
  constexpr ServiceDependency a_dependencies[] = {{"service.b", true}};
  constexpr ServiceDependency b_dependencies[] = {{"service.a", true}};
  ServiceDefinition service_a;
  service_a.id = "service.a";
  service_a.dependencies = a_dependencies;
  service_a.dependency_count = 1;
  ServiceDefinition service_b;
  service_b.id = "service.b";
  service_b.dependencies = b_dependencies;
  service_b.dependency_count = 1;
  register_or_fail(cycle_registry, service_a);
  register_or_fail(cycle_registry, service_b);
  CHECK(!cycle_registry.validate_graph(error));
  CHECK(error.rfind("service_dependency_cycle:", 0) == 0);
}

} // namespace

void test_dependency_failure_and_recovery() {
  using namespace lunaredu::lunaros;
  ServiceRegistry registry;
  std::string error;
  CHECK(registry.initialize(error));
  ServiceDefinition root; root.id = "root";
  register_or_fail(registry, root);
  ServiceDependency middle_dependencies[] = {{"root", true}};
  ServiceDefinition middle; middle.id = "middle";
  middle.dependencies = middle_dependencies; middle.dependency_count = 1;
  register_or_fail(registry, middle);
  ServiceDependency leaf_dependencies[] = {{"middle", true}, {"missing", false}};
  ServiceDefinition leaf; leaf.id = "leaf";
  leaf.criticality = ServiceCriticality::Required;
  leaf.dependencies = leaf_dependencies; leaf.dependency_count = 2;
  register_or_fail(registry, leaf);
  start_and_ready(registry, "root");
  start_and_ready(registry, "middle");
  start_and_ready(registry, "leaf");
  CHECK(registry.required_services_ready());
  CHECK(registry.mark_failed("root", "injected", error));
  CHECK(!registry.service_available("middle"));
  CHECK(!registry.service_available("leaf"));
  CHECK(!registry.required_services_ready());
  auto snapshot = registry.snapshot();
  CHECK(!snapshot.required_services_ready);
  CHECK(snapshot.services[2].state == ServiceState::Ready);
  CHECK(!snapshot.services[2].available);
  CHECK(!snapshot.services[2].dependencies[0].available);
  CHECK(registry.mark_ready("root", "recovered", error));
  CHECK(registry.service_available("leaf"));
  CHECK(registry.required_services_ready());
  CHECK(registry.snapshot().services[2].dependencies[0].available);
}

int main() {
  test_dependency_failure_and_recovery();
  test_dependency_order_and_required_readiness();
  test_stop_rejects_active_required_dependent();
  test_failure_recovery_and_snapshot();
  test_registration_validation();
  test_graph_rejects_missing_required_dependency_and_cycle();
  if (g_failures != 0) {
    std::cerr << g_failures << " LunarOS service checks failed" << std::endl;
    return 1;
  }
  std::cout << "6 LunarOS service lifecycle checks passed" << std::endl;
  return 0;
}
