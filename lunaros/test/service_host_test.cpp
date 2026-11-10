#include <iostream>
#include <string>

#include "kernel/services/service_host_internal.h"

namespace {

int g_failures = 0;
std::string g_dispatched_owner;
int g_dispatch_calls = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __func__ << ":" << __LINE__                              \
                << " check failed: " #condition << std::endl;                \
      ++g_failures;                                                            \
    }                                                                          \
  } while (false)

using namespace lunaredu::lunaros;

ServiceStartResult ready(void *context) {
  auto *calls = static_cast<int *>(context);
  ++*calls;
  return {ServiceStartDisposition::Ready, "ready"};
}

ServiceStartResult deferred(void *context) {
  auto *calls = static_cast<int *>(context);
  ++*calls;
  return {ServiceStartDisposition::Deferred, "queued"};
}

ServiceStartResult ready_without_count(void *) {
  return {ServiceStartDisposition::Ready, "ready"};
}

ServiceStartResult failed(void *) {
  return {ServiceStartDisposition::Failed, "broken"};
}

bool stopped(void *context, std::string &) {
  ++*static_cast<int *>(context);
  return true;
}

bool recording_dispatcher(const char *owner_executor,
                          ServiceLifecycleAction action, void *context,
                          std::string &error) {
  g_dispatched_owner = owner_executor != nullptr ? owner_executor : "";
  ++g_dispatch_calls;
  return action(context, error);
}

void test_dependency_order_and_deferred_completion() {
  ServiceRegistry registry;
  ServiceDefinition root;
  root.id = "root";
  root.criticality = ServiceCriticality::Required;
  std::string error;
  constexpr ServiceDependency dependencies[] = {{"root", true}};
  ServiceDefinition worker;
  worker.id = "worker";
  worker.criticality = ServiceCriticality::Required;
  worker.dependencies = dependencies;
  worker.dependency_count = 1;
  constexpr ServiceDependency child_dependencies[] = {{"worker", true}};
  ServiceDefinition child;
  child.id = "child";
  child.criticality = ServiceCriticality::Required;
  child.dependencies = child_dependencies;
  child.dependency_count = 1;
  int root_calls = 0;
  int worker_calls = 0;
  int child_calls = 0;
  ServiceHost host(registry);
  CHECK(host.install({worker, deferred, nullptr, &worker_calls},
                              error));
  CHECK(host.install({root, ready, nullptr, &root_calls}, error));
  CHECK(host.install({child, ready, nullptr, &child_calls}, error));
  CHECK(host.start_all(error));
  CHECK(root_calls == 1);
  CHECK(worker_calls == 1);
  CHECK(child_calls == 0);
  CHECK(!registry.required_services_ready());
  CHECK(host.report_ready("worker", "executor_ready", error));
  CHECK(child_calls == 1);
  CHECK(registry.required_services_ready());
}

void test_required_failure_stops_startup() {
  ServiceRegistry registry;
  ServiceDefinition service;
  service.id = "required";
  service.criticality = ServiceCriticality::Required;
  std::string error;
  ServiceHost host(registry);
  CHECK(host.install({service, failed, nullptr, nullptr}, error));
  CHECK(!host.start_all(error));
  CHECK(error == "service_start_failed:required:broken");
}

void test_optional_failure_does_not_stop_startup() {
  ServiceRegistry registry;
  ServiceDefinition service;
  service.id = "optional";
  std::string error;
  ServiceHost host(registry);
  CHECK(host.install({service, failed, nullptr, nullptr}, error));
  CHECK(host.start_all(error));
  CHECK(!registry.service_available("optional"));
}

void test_deferred_activation_starts_newly_available_dependents() {
  ServiceRegistry registry;
  ServiceDefinition root;
  root.id = "root";
  constexpr ServiceDependency dependencies[] = {{"root", true}};
  ServiceDefinition child;
  child.id = "child";
  child.dependencies = dependencies;
  child.dependency_count = 1;
  int root_calls = 0;
  int child_calls = 0;
  std::string error;
  ServiceHost host(registry);
  CHECK(host.install(
      {root, deferred, nullptr, &root_calls, ready}, error));
  CHECK(host.install(
      {child, ready, nullptr, &child_calls}, error));
  CHECK(host.start_all(error));
  CHECK(root_calls == 1);
  CHECK(child_calls == 0);
  CHECK(host.activate("root", error));
  CHECK(root_calls == 2);
  CHECK(child_calls == 1);
  CHECK(registry.service_available("root"));
  CHECK(registry.service_available("child"));
  CHECK(!host.activate("root", error));
  CHECK(error == "service_not_deferred:root");
}

void test_stop_all_uses_reverse_dependencies() {
  ServiceRegistry registry;
  ServiceDefinition root;
  root.id = "root";
  std::string error;
  constexpr ServiceDependency dependencies[] = {{"root", true}};
  ServiceDefinition child;
  child.id = "child";
  child.dependencies = dependencies;
  child.dependency_count = 1;
  int root_stops = 0;
  int child_stops = 0;
  ServiceHost host(registry);
  CHECK(host.install(
      {root, ready_without_count, stopped, &root_stops}, error));
  CHECK(host.install(
      {child, ready_without_count, stopped, &child_stops}, error));
  CHECK(host.start_all(error));
  CHECK(host.stop_all(error));
  CHECK(root_stops == 1);
  CHECK(child_stops == 1);
  const auto state = registry.snapshot();
  CHECK(state.services[0].state == ServiceState::Stopped);
  CHECK(state.services[1].state == ServiceState::Stopped);
}

void test_lifecycle_callbacks_use_declared_owner_dispatcher() {
  ServiceRegistry registry;
  ServiceDefinition service;
  service.id = "owned";
  service.owner_executor = "executor.custom";
  int starts = 0;
  std::string error;
  g_dispatched_owner.clear();
  g_dispatch_calls = 0;
  ServiceHost host(registry, recording_dispatcher);
  CHECK(host.install({service, ready, stopped, &starts}, error));
  CHECK(host.start_all(error));
  CHECK(starts == 1);
  CHECK(g_dispatched_owner == "executor.custom");
  CHECK(host.stop_all(error));
  CHECK(starts == 2);
  // Start and stop share one node context; this assertion proves both
  // lifecycle phases were dispatched through the declared owner.
  CHECK(g_dispatch_calls == 2);
}

} // namespace

int main() {
  test_dependency_order_and_deferred_completion();
  test_required_failure_stops_startup();
  test_optional_failure_does_not_stop_startup();
  test_deferred_activation_starts_newly_available_dependents();
  test_stop_all_uses_reverse_dependencies();
  test_lifecycle_callbacks_use_declared_owner_dispatcher();
  if (g_failures != 0) {
    std::cerr << g_failures << " ServiceHost checks failed" << std::endl;
    return 1;
  }
  std::cout << "6 ServiceHost orchestration checks passed" << std::endl;
  return 0;
}
