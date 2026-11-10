#include "kernel/services/service_registry_internal.h"

#include <memory>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>

#include "lunaredu_lunaros/kernel/log.h"
#endif

namespace lunaredu::lunaros {
namespace {

ServiceRegistry g_service_registry;

#ifdef ESP_PLATFORM
static_assert(sizeof(ServiceRegistry) <= 128,
              "ServiceRegistry control state must remain in internal SRAM; "
              "large service records belong in external storage");
#endif

bool state_is_available(ServiceState state) {
  return state == ServiceState::Ready || state == ServiceState::Degraded;
}

bool valid_transition(ServiceState current, ServiceState target) {
  switch (target) {
  case ServiceState::Starting:
    return current == ServiceState::Registered ||
           current == ServiceState::Stopped || current == ServiceState::Failed;
  case ServiceState::Ready:
  case ServiceState::Degraded:
    return current == ServiceState::Starting ||
           state_is_available(current) || current == ServiceState::Failed;
  case ServiceState::Failed:
    return current == ServiceState::Starting ||
           state_is_available(current) || current == ServiceState::Failed ||
           current == ServiceState::Stopping;
  case ServiceState::Stopping:
    return current == ServiceState::Starting ||
           state_is_available(current) || current == ServiceState::Failed;
  case ServiceState::Stopped:
    return current == ServiceState::Stopping;
  case ServiceState::Registered:
    return false;
  }
  return false;
}

} // namespace

ServiceRegistry::ServiceRegistry() {
#ifdef ESP_PLATFORM
  lock_ = xSemaphoreCreateMutexStatic(&lock_storage_);
#endif
}

bool ServiceRegistry::initialize(std::string &error) {
  Guard guard(lock_);
  error.clear();
  if (services_ != nullptr) {
    return true;
  }
#ifdef ESP_PLATFORM
  constexpr size_t kStorageBytes =
      sizeof(RegisteredService) * kServiceCapacity;
  void *storage = heap_caps_malloc(kStorageBytes,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (storage == nullptr || !esp_ptr_external_ram(storage)) {
    if (storage != nullptr) {
      heap_caps_free(storage);
    }
    error = "service_registry_external_storage_unavailable";
    return false;
  }
  auto *services = static_cast<RegisteredService *>(storage);
  for (size_t index = 0; index < kServiceCapacity; ++index) {
    std::construct_at(&services[index]);
  }
  // The registry owns stable metadata references used by snapshots. Keep the
  // backing array alive at one address for the entire boot, including service
  // stop/restart and failed product-composition retries.
  services_ = services;
  LOGI("lunaros.services", "registry storage=PSRAM bytes=%u",
       static_cast<unsigned>(kStorageBytes));
  return true;
#else
  error = "service_registry_storage_unavailable";
  return false;
#endif
}

void ServiceRegistry::reset() {
  Guard guard(lock_);
  if (services_ != nullptr) {
    for (size_t index = 0; index < service_count_; ++index) {
      services_[index] = {};
    }
  }
  service_count_ = 0;
  revision_ = 0;
}

bool ServiceRegistry::register_service(const ServiceDefinition &definition,
                                       std::string &error) {
  Guard guard(lock_);
  error.clear();
  if (services_ == nullptr) {
    error = "service_registry_not_initialized";
    return false;
  }
  if (definition.id == nullptr || definition.id[0] == '\0') {
    error = "invalid_service_id";
    return false;
  }
  if (find_service(definition.id) != nullptr) {
    error = "service_already_registered";
    return false;
  }
  if (service_count_ >= kServiceCapacity) {
    error = "service_registry_full";
    return false;
  }
  if (definition.dependency_count > kServiceDependencyCapacity) {
    error = "service_dependency_registry_full";
    return false;
  }
  if (definition.dependency_count > 0 && definition.dependencies == nullptr) {
    error = "invalid_service_dependencies";
    return false;
  }

  RegisteredService &service = services_[service_count_];
  service.id = definition.id;
  service.title = definition.title != nullptr && definition.title[0] != '\0'
                      ? definition.title
                      : definition.id;
  service.owner_executor = definition.owner_executor != nullptr
                               ? definition.owner_executor
                               : "lunar.main";
  service.criticality = definition.criticality;
  service.dependency_count = definition.dependency_count;
  for (size_t index = 0; index < definition.dependency_count; ++index) {
    const ServiceDependency &dependency = definition.dependencies[index];
    if (dependency.id == nullptr || dependency.id[0] == '\0' ||
        service.id == dependency.id) {
      service = {};
      error = "invalid_service_dependency";
      return false;
    }
    for (size_t previous = 0; previous < index; ++previous) {
      if (service.dependencies[previous].id == dependency.id) {
        service = {};
        error = "duplicate_service_dependency";
        return false;
      }
    }
    service.dependencies[index].id = dependency.id;
    service.dependencies[index].required = dependency.required;
  }
  service.state = ServiceState::Registered;
  service.revision = ++revision_;
  ++service_count_;
  return true;
}

bool ServiceRegistry::validate_graph(std::string &error) const {
  Guard guard(lock_);
  error.clear();
  if (services_ == nullptr) {
    error = "service_registry_not_initialized";
    return false;
  }
  std::array<uint8_t, kServiceCapacity> visit_state{};

  const auto service_index = [this](const std::string &id) -> size_t {
    for (size_t index = 0; index < service_count_; ++index) {
      if (services_[index].id == id) {
        return index;
      }
    }
    return kServiceCapacity;
  };

  const auto visit = [this, &visit_state, &service_index, &error](
                         auto &self, size_t index) -> bool {
    if (visit_state[index] == 2) {
      return true;
    }
    if (visit_state[index] == 1) {
      error = "service_dependency_cycle:" + services_[index].id;
      return false;
    }
    visit_state[index] = 1;
    const RegisteredService &service = services_[index];
    for (size_t dependency_index = 0;
         dependency_index < service.dependency_count; ++dependency_index) {
      const RegisteredService::Dependency &dependency =
          service.dependencies[dependency_index];
      const size_t target = service_index(dependency.id);
      if (target == kServiceCapacity) {
        if (dependency.required) {
          error = "service_dependency_not_registered:" + service.id + ":" +
                  dependency.id;
          return false;
        }
        continue;
      }
      if (dependency.required && !self(self, target)) {
        if (error.rfind("service_dependency_cycle:", 0) == 0) {
          error += "<-" + service.id;
        }
        return false;
      }
    }
    visit_state[index] = 2;
    return true;
  };

  for (size_t index = 0; index < service_count_; ++index) {
    if (!visit(visit, index)) {
      return false;
    }
  }
  return true;
}

bool ServiceRegistry::begin_start(const std::string &service_id,
                                  std::string &error) {
  Guard guard(lock_);
  RegisteredService *service = find_service(service_id);
  if (service == nullptr) {
    error = "service_not_found";
    return false;
  }
  if (!dependencies_available(*service, error)) {
    return false;
  }
  return transition_locked(service_id, ServiceState::Starting, "", error);
}

bool ServiceRegistry::mark_ready(const std::string &service_id,
                                 const char *detail, std::string &error) {
  Guard guard(lock_);
  return transition_locked(service_id, ServiceState::Ready, detail, error);
}

bool ServiceRegistry::mark_degraded(const std::string &service_id,
                                    const char *detail, std::string &error) {
  Guard guard(lock_);
  return transition_locked(service_id, ServiceState::Degraded, detail, error);
}

bool ServiceRegistry::mark_failed(const std::string &service_id,
                                  const char *detail, std::string &error) {
  Guard guard(lock_);
  return transition_locked(service_id, ServiceState::Failed, detail, error);
}

bool ServiceRegistry::begin_stop(const std::string &service_id,
                                 std::string &error) {
  Guard guard(lock_);
  const RegisteredService *service = find_service(service_id);
  if (service == nullptr) {
    error = "service_not_found";
    return false;
  }
  for (size_t index = 0; index < service_count_; ++index) {
    const RegisteredService &candidate = services_[index];
    if (!state_is_available(candidate.state) &&
        candidate.state != ServiceState::Starting) {
      continue;
    }
    for (size_t dependency_index = 0;
         dependency_index < candidate.dependency_count; ++dependency_index) {
      const auto &dependency = candidate.dependencies[dependency_index];
      if (dependency.required && dependency.id == service->id) {
        error = "service_has_active_dependent:" + candidate.id;
        return false;
      }
    }
  }
  return transition_locked(service_id, ServiceState::Stopping, "", error);
}

bool ServiceRegistry::mark_stopped(const std::string &service_id,
                                   const char *detail, std::string &error) {
  Guard guard(lock_);
  return transition_locked(service_id, ServiceState::Stopped, detail, error);
}

bool ServiceRegistry::service_registered(const std::string &service_id) const {
  Guard guard(lock_);
  return find_service(service_id) != nullptr;
}

bool ServiceRegistry::service_available(const std::string &service_id) const {
  Guard guard(lock_);
  const RegisteredService *service = find_service(service_id);
  return service != nullptr && effectively_available(*service);
}

bool ServiceRegistry::required_services_ready(std::string *reason) const {
  Guard guard(lock_);
  if (reason != nullptr) {
    reason->clear();
  }
  for (size_t index = 0; index < service_count_; ++index) {
    const RegisteredService &service = services_[index];
    if (service.criticality == ServiceCriticality::Required &&
        !effectively_available(service)) {
      if (reason != nullptr) {
        *reason = "required_service_unavailable:" + service.id;
      }
      return false;
    }
  }
  return true;
}

ServiceRegistrySnapshot ServiceRegistry::snapshot() const {
  Guard guard(lock_);
  ServiceRegistrySnapshot result;
  result.revision = revision_;
  result.required_services_ready = true;
  for (size_t index = 0; index < service_count_; ++index) {
    if (services_[index].criticality == ServiceCriticality::Required &&
        !effectively_available(services_[index])) {
      result.required_services_ready = false;
      break;
    }
  }
  result.service_count = service_count_;
  for (size_t index = 0; index < service_count_; ++index) {
    const RegisteredService &source = services_[index];
    ServiceSnapshot &target = result.services[index];
    target.id = source.id.c_str();
    target.title = source.title.c_str();
    target.owner_executor = source.owner_executor.c_str();
    target.criticality = source.criticality;
    target.state = source.state;
    target.available = effectively_available(source);
    target.detail = source.detail;
    target.revision = source.revision;
    target.dependency_count = source.dependency_count;
    for (size_t dependency_index = 0;
         dependency_index < source.dependency_count; ++dependency_index) {
      target.dependencies[dependency_index].id =
          source.dependencies[dependency_index].id.c_str();
      target.dependencies[dependency_index].required =
          source.dependencies[dependency_index].required;
      const RegisteredService *dependency =
          find_service(source.dependencies[dependency_index].id);
      target.dependencies[dependency_index].available =
          dependency != nullptr && effectively_available(*dependency);
    }
  }
  return result;
}

ServiceRegistry::RegisteredService *
ServiceRegistry::find_service(const std::string &service_id) {
  if (services_ == nullptr) {
    return nullptr;
  }
  for (size_t index = 0; index < service_count_; ++index) {
    if (services_[index].id == service_id) {
      return &services_[index];
    }
  }
  return nullptr;
}

const ServiceRegistry::RegisteredService *
ServiceRegistry::find_service(const std::string &service_id) const {
  if (services_ == nullptr) {
    return nullptr;
  }
  for (size_t index = 0; index < service_count_; ++index) {
    if (services_[index].id == service_id) {
      return &services_[index];
    }
  }
  return nullptr;
}

bool ServiceRegistry::transition_locked(const std::string &service_id,
                                        ServiceState target,
                                        const char *detail,
                                        std::string &error) {
  error.clear();
  RegisteredService *service = find_service(service_id);
  if (service == nullptr) {
    error = "service_not_found";
    return false;
  }
  if (!valid_transition(service->state, target)) {
    error = "invalid_service_transition:" +
            std::string(service_state_name(service->state)) + ":" +
            service_state_name(target);
    return false;
  }
  service->state = target;
  service->detail = detail != nullptr ? detail : "";
  service->revision = ++revision_;
  return true;
}

bool ServiceRegistry::effectively_available(const RegisteredService &service) const {
  std::array<int8_t, kServiceCapacity> cache;
  cache.fill(-1);
  return effectively_available(service, cache);
}

bool ServiceRegistry::effectively_available(
    const RegisteredService &service, std::array<int8_t, kServiceCapacity> &cache) const {
  // A visiting node is unavailable until all required dependencies pass. This
  // also rejects cycles before graph validation, without repeated DAG walks.
  auto &available = cache[static_cast<size_t>(&service - services_)];
  if (available >= 0) return available != 0;
  available = 0;
  if (!state_is_available(service.state)) return false;
  for (size_t index = 0; index < service.dependency_count; ++index) {
    const auto &dependency = service.dependencies[index];
    if (!dependency.required) {
      continue;
    }
    const auto *target = find_service(dependency.id);
    if (target == nullptr || !effectively_available(*target, cache)) {
      return false;
    }
  }
  available = 1;
  return true;
}

bool ServiceRegistry::dependencies_available(
    const RegisteredService &service, std::string &reason) const {
  for (size_t index = 0; index < service.dependency_count; ++index) {
    const RegisteredService::Dependency &dependency =
        service.dependencies[index];
    if (!dependency.required) {
      continue;
    }
    const RegisteredService *target = find_service(dependency.id);
    if (target == nullptr || !effectively_available(*target)) {
      reason = "service_dependency_unavailable:" + dependency.id;
      return false;
    }
  }
  reason.clear();
  return true;
}

ServiceRegistry &service_registry_internal() { return g_service_registry; }

void reset_service_registry_internal() { g_service_registry.reset(); }

bool service_available(const char *service_id) {
  return service_id != nullptr &&
         g_service_registry.service_available(service_id);
}

bool required_services_ready(std::string *reason) {
  return g_service_registry.required_services_ready(reason);
}

ServiceRegistrySnapshot service_registry_snapshot() {
  return g_service_registry.snapshot();
}

} // namespace lunaredu::lunaros
