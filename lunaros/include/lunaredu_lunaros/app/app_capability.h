#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#ifndef CONFIG_LUNAREDU_LUNAROS_CAPABILITY_CONSUMERS
#define CONFIG_LUNAREDU_LUNAROS_CAPABILITY_CONSUMERS 16
#endif

namespace lunaredu::app {

// Native application capabilities are independent of foreground pages and HTTP
// device projections. Descriptors and their strings must have static lifetime.
// Handlers consume borrowed input synchronously and synchronize their own state.
// Long operations return an application-owned activity handle in their result.
struct CapabilityCall {
  const char *caller = nullptr;
  uint32_t contract = 0;
  const void *request = nullptr;
  size_t request_size = 0;
  void *response = nullptr;
  size_t response_size = 0;
};

using CapabilityHandler = bool (*)(void *, const CapabilityCall &, std::string &);
using CapabilityAvailability = bool (*)(void *);

struct CapabilityDefinition {
  const char *id = nullptr;
  const char *provider_app = nullptr;
  uint32_t contract = 0;
  size_t request_size = 0;
  size_t response_size = 0;
  CapabilityHandler invoke = nullptr;
  CapabilityAvailability available = nullptr;
  void *context = nullptr;
  const char *const *allowed_callers = nullptr;
  size_t allowed_caller_count = 0;
};

inline constexpr size_t kCallableCapabilityCapacity = 64;
using CapabilityVisitor = void (*)(const CapabilityDefinition &, bool, void *);

// Dependencies describe contracts, not foreground pages. Missing optional
// providers are valid (for example a Pet product without a camera). All
// descriptor storage is borrowed and must remain immutable for firmware life.
struct CapabilityDependency {
  const char *id = nullptr;
  uint32_t contract = 0;
  bool required = true;
};
struct CapabilityConsumerDefinition {
  const char *app_id = nullptr;
  const CapabilityDependency *dependencies = nullptr;
  size_t dependency_count = 0;
};
inline constexpr size_t kCapabilityConsumerCapacity = CONFIG_LUNAREDU_LUNAROS_CAPABILITY_CONSUMERS;
inline constexpr size_t kCapabilityDependencyCapacity = 32;

// A product can narrow provider ACLs; it can never grant access rejected by
// the provider. This trusted, synchronous policy must be thread-safe and must
// not re-enter the registry. Callers must come from trusted host code, never
// request JSON. This is native API governance, not a memory-isolation boundary.
using CapabilityAccessPolicy = bool (*)(const char *caller,
                                       const CapabilityDefinition &, void *);
bool configure_capability_access(CapabilityAccessPolicy, void *context,
                                 std::string &error);
bool register_capability_consumer(const CapabilityConsumerDefinition &,
                                  std::string &error);
// Seal checks required contracts and authorization, never hardware readiness.
// It rejects new providers/consumers; identical startup declarations remain
// idempotent so services can restart without changing the catalog.
bool seal_capabilities(std::string &error);
bool capabilities_sealed();
// Composition rollback only, before any executor or external serving starts.
void reset_capabilities();

// Registration is append-only, before executor startup. Re-registering the
// exact definition is idempotent; a different owner/handler is rejected.
bool register_capability(const CapabilityDefinition &, std::string &error);
bool invoke_capability(const char *id, const CapabilityCall &, std::string &error);
bool capability_available(const char *id, const char *caller);
// Read caller-visible metadata without evaluating application callbacks.
bool find_capability(const char *id, const char *caller, CapabilityDefinition &out);
// Filter before evaluating availability: discovery must not re-enter unrelated apps.
void visit_capabilities(const char *caller, CapabilityVisitor, void *context,
                        uint32_t contract_filter = 0, bool evaluate_availability = true);

template <typename Request, typename Response>
bool call_capability(const char *id, const char *caller, uint32_t contract,
                     const Request &request, Response &response, std::string &error) {
  return invoke_capability(id, {caller, contract, &request, sizeof(Request),
                                &response, sizeof(Response)}, error);
}

} // namespace lunaredu::app
