#include "lunaredu_lunaros/app/app_capability.h"
#include "capability_registration.h"

#include <array>
#include <cstring>
#include <mutex>

namespace lunaredu::app {
namespace {
std::mutex mutex;
std::array<CapabilityDefinition, kCallableCapabilityCapacity> definitions{};
size_t count = 0;
std::array<CapabilityConsumerDefinition, kCapabilityConsumerCapacity> consumers{};
size_t consumer_count = 0;
bool sealed = false;
CapabilityAccessPolicy access_policy = nullptr;
void *access_context = nullptr;

bool text_valid(const char *text) { return text != nullptr && text[0] != '\0'; }
bool provider_allows(const CapabilityDefinition &definition, const char *caller) {
  if (!text_valid(caller)) return false;
  for (size_t i = 0; i < definition.allowed_caller_count; ++i) {
    if (std::strcmp(caller, definition.allowed_callers[i]) == 0) return true;
  }
  return false;
}
bool allowed(const CapabilityDefinition &definition, const char *caller) {
  if (!provider_allows(definition, caller)) return false;
  CapabilityAccessPolicy policy;
  void *context;
  {
    std::lock_guard<std::mutex> lock(mutex);
    policy = access_policy;
    context = access_context;
  }
  return !policy || policy(caller, definition, context);
}
bool lookup(const char *id, CapabilityDefinition &out) {
  if (!text_valid(id)) return false;
  std::lock_guard<std::mutex> lock(mutex);
  for (size_t i = 0; i < count; ++i) {
    if (std::strcmp(id, definitions[i].id) == 0) {
      out = definitions[i];
      return true;
    }
  }
  return false;
}
bool fail(std::string &error, const char *message) { error = message; return false; }
} // namespace

bool declare_capabilities_atomically(bool (*declare)(std::string &),
                                     std::string &error) {
  if (!declare) return true;
  size_t original_count;
  size_t original_consumer_count;
  {
    std::lock_guard<std::mutex> lock(mutex);
    original_count = count;
    original_consumer_count = consumer_count;
  }
  // Do not hold the registry mutex across application code. Registration is
  // append-only and product composition has a single owner before startup.
  if (declare(error)) return true;
  std::lock_guard<std::mutex> lock(mutex);
  while (count > original_count) definitions[--count] = {};
  while (consumer_count > original_consumer_count) consumers[--consumer_count] = {};
  return false;
}

bool register_capability(const CapabilityDefinition &definition, std::string &error) {
  if (!text_valid(definition.id) || !text_valid(definition.provider_app) ||
      definition.contract == 0 || definition.request_size == 0 ||
      definition.response_size == 0 || definition.invoke == nullptr ||
      definition.allowed_callers == nullptr || definition.allowed_caller_count == 0) {
    return fail(error, "invalid_application_capability");
  }
  for (size_t i = 0; i < definition.allowed_caller_count; ++i) {
    if (!text_valid(definition.allowed_callers[i])) return fail(error, "invalid_capability_caller");
    for (size_t j = 0; j < i; ++j)
      if (std::strcmp(definition.allowed_callers[i], definition.allowed_callers[j]) == 0)
        return fail(error, "duplicate_capability_caller");
  }
  std::lock_guard<std::mutex> lock(mutex);
  for (size_t i = 0; i < count; ++i) {
    const auto &existing = definitions[i];
    if (std::strcmp(existing.id, definition.id) != 0) continue;
    bool same = std::strcmp(existing.provider_app, definition.provider_app) == 0 &&
        existing.contract == definition.contract && existing.request_size == definition.request_size &&
        existing.response_size == definition.response_size && existing.invoke == definition.invoke &&
        existing.available == definition.available && existing.context == definition.context &&
        existing.allowed_caller_count == definition.allowed_caller_count;
    for (size_t j = 0; same && j < existing.allowed_caller_count; ++j)
      same = std::strcmp(existing.allowed_callers[j], definition.allowed_callers[j]) == 0;
    if (!same) return fail(error, "application_capability_conflict");
    error.clear();
    return true;
  }
  if (sealed) return fail(error, "application_capability_registry_sealed");
  if (count == definitions.size()) return fail(error, "application_capability_registry_full");
  definitions[count++] = definition;
  error.clear();
  return true;
}

bool configure_capability_access(CapabilityAccessPolicy policy, void *context,
                                 std::string &error) {
  std::lock_guard<std::mutex> lock(mutex);
  if (sealed) return fail(error, "application_capability_registry_sealed");
  if (!policy && context) return fail(error, "invalid_capability_access_policy");
  access_policy = policy;
  access_context = context;
  error.clear();
  return true;
}

bool register_capability_consumer(const CapabilityConsumerDefinition &consumer,
                                  std::string &error) {
  if (!text_valid(consumer.app_id) ||
      consumer.dependency_count > kCapabilityDependencyCapacity ||
      (consumer.dependency_count && !consumer.dependencies))
    return fail(error, "invalid_capability_consumer");
  for (size_t i = 0; i < consumer.dependency_count; ++i) {
    const auto &dependency = consumer.dependencies[i];
    if (!text_valid(dependency.id) || !dependency.contract)
      return fail(error, "invalid_capability_dependency");
    for (size_t j = 0; j < i; ++j)
      if (std::strcmp(dependency.id, consumer.dependencies[j].id) == 0)
        return fail(error, "duplicate_capability_dependency");
  }
  std::lock_guard<std::mutex> lock(mutex);
  for (size_t i = 0; i < consumer_count; ++i) {
    const auto &existing = consumers[i];
    if (std::strcmp(existing.app_id, consumer.app_id) != 0) continue;
    bool same = existing.dependency_count == consumer.dependency_count;
    for (size_t j = 0; same && j < existing.dependency_count; ++j) {
      const auto &a = existing.dependencies[j];
      const auto &b = consumer.dependencies[j];
      same = std::strcmp(a.id, b.id) == 0 && a.contract == b.contract && a.required == b.required;
    }
    if (!same) return fail(error, "capability_consumer_conflict");
    error.clear();
    return true;
  }
  if (sealed) return fail(error, "application_capability_registry_sealed");
  if (consumer_count == consumers.size()) return fail(error, "capability_consumer_registry_full");
  consumers[consumer_count++] = consumer;
  error.clear();
  return true;
}

bool seal_capabilities(std::string &error) {
  // This is a boot-only transaction: composition is serialized by the product.
  // Application policy callbacks are deliberately evaluated without our mutex.
  size_t limit;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (sealed) { error.clear(); return true; }
    limit = consumer_count;
  }
  for (size_t i = 0; i < limit; ++i) {
    CapabilityConsumerDefinition consumer;
    { std::lock_guard<std::mutex> lock(mutex); consumer = consumers[i]; }
    for (size_t j = 0; j < consumer.dependency_count; ++j) {
      const auto &dependency = consumer.dependencies[j];
      CapabilityDefinition provider;
      const bool found = lookup(dependency.id, provider);
      const char *reason = !found ? "missing" :
          provider.contract != dependency.contract ? "contract_mismatch" :
          !allowed(provider, consumer.app_id) ? "denied" : nullptr;
      if (reason && (dependency.required || (found && provider.contract != dependency.contract))) {
        error = std::string("capability_dependency_") + reason + ":" + consumer.app_id + ":" + dependency.id;
        return false;
      }
    }
  }
  std::lock_guard<std::mutex> lock(mutex);
  sealed = true;
  error.clear();
  return true;
}

bool capabilities_sealed() {
  std::lock_guard<std::mutex> lock(mutex);
  return sealed;
}

void reset_capabilities() {
  std::lock_guard<std::mutex> lock(mutex);
  definitions = {};
  consumers = {};
  count = consumer_count = 0;
  access_policy = nullptr;
  access_context = nullptr;
  sealed = false;
}

bool invoke_capability(const char *id, const CapabilityCall &call, std::string &error) {
  CapabilityDefinition definition;
  if (!lookup(id, definition)) return fail(error, "application_capability_not_found");
  if (!allowed(definition, call.caller)) return fail(error, "application_capability_denied");
  if (call.contract != definition.contract || call.request_size != definition.request_size ||
      call.response_size != definition.response_size || !call.request || !call.response)
    return fail(error, "application_capability_contract_mismatch");
  // Never hold the registry lock across application code or nested calls.
  if (definition.available && !definition.available(definition.context))
    return fail(error, "application_capability_unavailable");
  error.clear();
  return definition.invoke(definition.context, call, error);
}

bool capability_available(const char *id, const char *caller) {
  CapabilityDefinition definition;
  return lookup(id, definition) && allowed(definition, caller) &&
      (!definition.available || definition.available(definition.context));
}

bool find_capability(const char *id, const char *caller, CapabilityDefinition &out) {
  CapabilityDefinition definition;
  if (!lookup(id, definition) || !allowed(definition, caller)) return false;
  out = definition;
  return true;
}

void visit_capabilities(const char *caller, CapabilityVisitor visitor, void *context, uint32_t contract_filter,
                        bool evaluate_availability) {
  if (!visitor) return;
  size_t limit;
  { std::lock_guard<std::mutex> lock(mutex); limit = count; }
  for (size_t i = 0; i < limit; ++i) {
    CapabilityDefinition definition;
    { std::lock_guard<std::mutex> lock(mutex); definition = definitions[i]; }
    if ((!contract_filter || definition.contract == contract_filter) && allowed(definition, caller))
      visitor(definition, evaluate_availability &&
          (!definition.available || definition.available(definition.context)), context);
  }
}
} // namespace lunaredu::app
