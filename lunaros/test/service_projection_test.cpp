#include <cassert>
#include <iostream>
#include "kernel/services/service_registry_internal.h"
#include "lunaredu_lunaros/device_api/device_projection.h"
#include "lunaredu_lunaros/device_api/standard_projections.h"

using namespace lunaredu::lunaros;
namespace { const DeviceModuleProjection *projection = nullptr; }
namespace lunaredu::lunaros {
bool register_device_projection(const DeviceProjectionContribution &value, std::string *) {
  projection = value.module;
  return true;
}
}
int main() {
  std::string error;
  auto &registry = service_registry_internal();
  assert(registry.initialize(error));
  ServiceDefinition provider; provider.id = "provider";
  assert(registry.register_service(provider, error));
  assert(registry.begin_start(provider.id, error));
  assert(registry.mark_ready(provider.id, "ready", error));
  ServiceDependency dependencies[] = {{"provider", true}};
  ServiceDefinition consumer; consumer.id = "consumer";
  consumer.criticality = ServiceCriticality::Required;
  consumer.dependencies = dependencies; consumer.dependency_count = 1;
  assert(registry.register_service(consumer, error));
  assert(registry.begin_start(consumer.id, error));
  assert(registry.mark_ready(consumer.id, "ready", error));
  assert(registry.mark_failed(provider.id, "injected", error));
  assert(setup_service_projection(error));
  assert(projection && projection->operation_count == 2);
  for (size_t i = 0; i < projection->operation_count; ++i) {
    assert(projection->operations[i].model.execution == DeviceOperationExecution::Caller);
    assert(projection->operations[i].model.owner_executor == nullptr);
  }
  // There is deliberately no executor implementation or bound main task in
  // this test. Registry diagnostics must remain independently callable.
  JsonDocument document;
  projection->operations[1].projection.json_provider(document.to<JsonObject>());
  assert(!document["required_services_ready"].as<bool>());
  assert(document["services"][1]["state"].as<std::string>() == "ready");
  assert(!document["services"][1]["available"].as<bool>());
  assert(!document["services"][1]["dependencies"][0]["available"].as<bool>());
  std::cout << "Service diagnostics without main executor and effective availability passed\n";
}
