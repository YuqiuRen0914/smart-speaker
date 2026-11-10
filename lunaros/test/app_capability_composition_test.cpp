#include "lunaredu_lunaros/app/app_capability.h"
#include <cassert>
#include <iostream>
#include <string>
using namespace lunaredu::app;
namespace {
bool online = false;
bool grant = true;
int readiness_calls = 0, invocations = 0;
bool ready(void *) { ++readiness_calls; return online; }
bool execute(void *, const CapabilityCall &call, std::string &) {
  ++invocations;
  *static_cast<int *>(call.response) = *static_cast<const int *>(call.request) + 1;
  return true;
}
bool access(const char *, const CapabilityDefinition &, void *) { return grant; }
const char *const callers[] = {"pet"};
const CapabilityDefinition provider{
    "test.capture", "camera", 7, sizeof(int), sizeof(int), execute, ready, nullptr, callers, 1};
const CapabilityDependency required[] = {{"test.capture", 7, true}};
const CapabilityDependency optional[] = {{"missing.camera", 1, false}};
}
int main() {
  std::string error;
  int input = 3, output = 0;
  reset_capabilities();
  assert(register_capability_consumer({"pet", required, 1}, error));
  assert(!seal_capabilities(error));
  assert(error == "capability_dependency_missing:pet:test.capture");
  assert(!capabilities_sealed());
  assert(register_capability(provider, error));
  assert(register_capability_consumer({"other", optional, 1}, error));
  assert(configure_capability_access(access, nullptr, error));
  grant = false;
  assert(!seal_capabilities(error));
  assert(error == "capability_dependency_denied:pet:test.capture");
  grant = true;
  assert(seal_capabilities(error)); // Offline hardware never prevents composition.
  assert(readiness_calls == 0 && invocations == 0);
  assert(seal_capabilities(error));
  assert(register_capability(provider, error)); // Service restart, identical declaration.
  assert(register_capability_consumer({"pet", required, 1}, error));
  auto late = provider; late.id = "late";
  assert(!register_capability(late, error));
  assert(error == "application_capability_registry_sealed");
  assert(!register_capability_consumer({"late", nullptr, 0}, error));
  assert(!configure_capability_access(nullptr, nullptr, error));
  auto changed = provider; changed.provider_app = "replacement";
  assert(!register_capability(changed, error));
  assert(!call_capability(provider.id, "stranger", 7, input, output, error));
  assert(readiness_calls == 0 && invocations == 0);
  assert(!call_capability(provider.id, "pet", 8, input, output, error));
  assert(readiness_calls == 0);
  assert(!call_capability(provider.id, "pet", 7, input, output, error));
  assert(error == "application_capability_unavailable");
  online = true;
  assert(call_capability(provider.id, "pet", 7, input, output, error));
  assert(output == 4 && invocations == 1);
  grant = false; // Every call/discovery must recheck dynamic policy.
  CapabilityDefinition found;
  assert(!find_capability(provider.id, "pet", found));
  assert(!capability_available(provider.id, "pet"));
  assert(!call_capability(provider.id, "pet", 7, input, output, error));
  int visited = 0;
  visit_capabilities("pet", [](const CapabilityDefinition &, bool, void *ctx) {
    ++*static_cast<int *>(ctx);
  }, &visited);
  assert(visited == 0 && invocations == 1);
  reset_capabilities();
  assert(!capabilities_sealed() && !find_capability(provider.id, "pet", found));
  assert(register_capability(provider, error));
  const CapabilityDependency wrong_version[] = {{"test.capture", 8, false}};
  assert(register_capability_consumer({"pet", wrong_version, 1}, error));
  assert(!seal_capabilities(error));
  assert(error == "capability_dependency_contract_mismatch:pet:test.capture");
  reset_capabilities();
  const CapabilityDependency duplicates[] = {{"x", 1, false}, {"x", 1, true}};
  assert(!register_capability_consumer({"pet", duplicates, 2}, error));
  auto bad = provider;
  const char *const repeated[] = {"pet", "pet"};
  bad.allowed_callers = repeated; bad.allowed_caller_count = 2;
  assert(!register_capability(bad, error));
  // Bounded consumer table and rollback, with no invocation-time allocations
  // introduced by metadata ownership.
  std::string ids[kCapabilityConsumerCapacity + 1];
  for (size_t i = 0; i < kCapabilityConsumerCapacity; ++i) {
    ids[i] = std::to_string(i);
    assert(register_capability_consumer({ids[i].c_str(), nullptr, 0}, error));
  }
  assert(!register_capability_consumer({"overflow", nullptr, 0}, error));
  assert(error == "capability_consumer_registry_full");
  reset_capabilities();
  std::cout << "Capability composition: contracts, ACL intersection, sealing, readiness and rollback passed\n";
}
