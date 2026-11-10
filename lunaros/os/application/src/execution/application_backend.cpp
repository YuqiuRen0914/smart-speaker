#include "app/execution/backend_catalog.h"
#include "app/execution/application_backend.h"
#include "app/package/package_signature.h"
#include <array>
#include <mutex>
#include <cstring>
namespace lunaredu::lunaros::apps {
namespace {
struct Backend {
  std::string id;
  ApplicationSessionFactory factory = nullptr;
  ApplicationEntryValidator validate = nullptr;
  bool (*available)() = nullptr;
  const ApplicationArtifactProfile *profile = nullptr;
};
std::array<Backend, 8> backends{};
std::mutex backend_mutex;
struct BackendView {
  ApplicationSessionFactory factory=nullptr;
  ApplicationEntryValidator validate=nullptr;
  bool (*available)()=nullptr;
  const ApplicationArtifactProfile *profile=nullptr;
  bool aot=false;
};
BackendView lookup(const std::string &id) {
  std::lock_guard lock(backend_mutex);
  for (const auto &backend : backends) if (backend.id == id) return {backend.factory,backend.validate,backend.available,backend.profile,backend.id=="aot"};
  return {};
}
bool available(const BackendView &backend) {
  return backend.factory && (!backend.available || backend.available()) &&
      (!backend.aot || (backend.profile&&aot_trust_available()));
}
}
bool register_application_backend(const char *id, ApplicationSessionFactory factory,
                                  ApplicationEntryValidator validator, bool (*enabled)(),
                                  const ApplicationArtifactProfile *profile) {
  if (!id || !*id || (!factory && !validator)) return false;
  if (profile && (!profile->target || !*profile->target || !profile->cpu || !*profile->cpu ||
      !profile->runtime_revision || !*profile->runtime_revision || !profile->protection_profile ||
      !*profile->protection_profile || !profile->abi || !profile->format)) return false;
  std::lock_guard lock(backend_mutex);
  for (auto &backend : backends) if (backend.id == id) {
    if ((factory && backend.factory && backend.factory != factory) ||
        (validator && backend.validate && backend.validate != validator) ||
        (enabled && backend.available && backend.available != enabled) ||
        (profile && backend.profile && (profile->abi!=backend.profile->abi || profile->format!=backend.profile->format ||
          std::strcmp(profile->target,backend.profile->target) || std::strcmp(profile->cpu,backend.profile->cpu) ||
          std::strcmp(profile->runtime_revision,backend.profile->runtime_revision) ||
          std::strcmp(profile->protection_profile,backend.profile->protection_profile)))) return false;
    if (factory) backend.factory = factory;
    if (validator) backend.validate = validator;
    if (enabled) backend.available = enabled;
    if (profile) backend.profile = profile;
    return true;
  }
  if (!validator) return false;
  for (auto &backend : backends) if (backend.id.empty()) {
    backend = {id, factory, validator, enabled, profile};
    return true;
  }
  return false;
}
const ApplicationArtifactProfile *application_artifact_profile(const std::string &id) { return lookup(id).profile; }
bool application_backend_available(const std::string &id) { return available(lookup(id)); }
ApplicationExecutionCapabilities application_execution_capabilities() {
  ApplicationExecutionCapabilities result;
  result.wasm = application_backend_available("wasm");
  result.micropython = application_backend_available("micropython");
  result.aot = application_backend_available("aot");
  return result;
}
std::unique_ptr<ApplicationSession> create_application_session(const std::string &id,
                                                             KernelRuntime &runtime) {
  const auto backend = lookup(id);
  return available(backend) ? backend.factory(runtime) : nullptr;
}
bool validate_application_entry(const std::string &id, const uint8_t *bytes, size_t size) {
  const auto backend = lookup(id);
  return backend.validate && backend.validate(bytes, size);
}
}
