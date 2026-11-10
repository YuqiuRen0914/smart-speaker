#pragma once

#include <array>
#include <mutex>
#include <cstddef>
#include <cstdint>
#include <string>
#include "model/operation_outcome.h"
#include "model/operation_context.h"

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
class CapabilityRegistry;
struct CapabilityCall {
  const char *caller = nullptr;
  uint32_t contract = 0;
  const void *request = nullptr;
  size_t request_size = 0;
  void *response = nullptr;
  size_t response_size = 0;
  CapabilityRegistry *catalog = nullptr; // Set by dispatcher, never trusted from callers.
  // Optional explicit diagnostic context. It conveys no authority.
  uint64_t diagnostic_parent_trace_id = 0;
  uint32_t diagnostic_generation = 0;
  uint32_t diagnostic_request_sequence = 0;
  // Optional synchronous result channel. The provider may distinguish domain
  // acceptance from completion; this pointer never crosses an async boundary.
  lunaredu::lunaros::OperationOutcome *outcome=nullptr;
  lunaredu::lunaros::OperationContext execution{};
};

using CapabilityHandler = bool (*)(void *, const CapabilityCall &, std::string &);
using CapabilityAvailability = bool (*)(CapabilityRegistry &, void *);
using CapabilityAvailabilityDetail = bool (*)(CapabilityRegistry &,void *,std::string &);

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
  lunaredu::lunaros::OperationExecutor executor=lunaredu::lunaros::OperationExecutor::Main;
  CapabilityAvailabilityDetail availability_detail=nullptr;
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
// Trusted composition port. invoke must retain all borrowed arguments until
// the callback physically returns once accepted; false before start is safe.
// A timeout bounds admission only, never releases an executing stack frame.
struct CapabilityExecutionPort {
  void *context=nullptr;
  bool (*owns)(void *,lunaros::OperationExecutor)=nullptr;
  bool (*invoke)(void *,lunaros::OperationExecutor,void (*)(void *),void *,uint32_t,std::string &)=nullptr;
};
class AppController;
// One bounded catalog owned by AppController. No process-global registry.
class CapabilityRegistry {
public:
  CapabilityRegistry() = default;
  CapabilityRegistry(const CapabilityRegistry &) = delete;
  CapabilityRegistry &operator=(const CapabilityRegistry &) = delete;
  bool configure_capability_access(CapabilityAccessPolicy, void *context,
                                   std::string &error);
  bool configure_capability_execution(CapabilityExecutionPort,std::string &error);
  bool register_capability_consumer(const CapabilityConsumerDefinition &,
                                    std::string &error);
  // Seal checks required contracts and authorization, never hardware readiness.
  // It rejects new providers/consumers; identical startup declarations remain
  // idempotent so services can restart without changing the catalog.
  bool seal_capabilities(std::string &error);
  bool capabilities_sealed();
  // Registration is append-only, before executor startup. Re-registering the
  // exact definition is idempotent; a different owner/handler is rejected.
  bool register_capability(const CapabilityDefinition &, std::string &error);
  bool invoke_capability(const char *id, const CapabilityCall &, std::string &error);
  bool capability_available(const char *id, const char *caller,std::string *reason=nullptr);
  // Read caller-visible metadata without evaluating application callbacks.
  bool find_capability(const char *id, const char *caller, CapabilityDefinition &out);
  // Filter before evaluating availability: discovery must not re-enter unrelated apps.
  void visit_capabilities(const char *caller, CapabilityVisitor, void *context,
                          uint32_t contract_filter = 0, bool evaluate_availability = true);

  template <typename Request, typename Response>
  bool call_capability(const char *id, const char *caller, uint32_t contract,
                       const Request &request, Response &response, std::string &error,
                       lunaredu::lunaros::OperationOutcome *outcome=nullptr) {
    CapabilityCall call{caller,contract,&request,sizeof(Request),&response,sizeof(Response)};
    call.outcome=outcome;
    call.execution=lunaros::capture_operation_context();
    return invoke_capability(id,call,error);
  }

private:
  friend class AppController;
  void reset_capabilities();
  bool declare_atomically(bool (*declare)(CapabilityRegistry &, std::string &), std::string &error);
  bool allowed(const CapabilityDefinition &, const char *caller);
  bool lookup(const char *id, CapabilityDefinition &out);
  bool evaluate_availability(const CapabilityDefinition &,std::string *reason=nullptr);
  std::mutex mutex;
  std::array<CapabilityDefinition, kCallableCapabilityCapacity> definitions{};
  size_t count = 0;
  std::array<CapabilityConsumerDefinition, kCapabilityConsumerCapacity> consumers{};
  size_t consumer_count = 0;
  bool sealed = false;
  CapabilityAccessPolicy access_policy = nullptr;
  void *access_context = nullptr;
  CapabilityExecutionPort execution_port{};
};

} // namespace lunaredu::app
