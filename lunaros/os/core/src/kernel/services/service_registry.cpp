#include "kernel/services/service_registry_internal.h"

#include <memory>
#include <string_view>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>

#endif

namespace lunaredu::lunaros {

namespace {

uint32_t service_hash(std::string_view id) {
  uint32_t hash=2166136261U;
  for(const unsigned char byte:id)hash=(hash^byte)*16777619U;
  return hash;
}

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
           state_is_available(current) || current == ServiceState::Failed ||
           current == ServiceState::Stopping;
  case ServiceState::Stopped:
    return current == ServiceState::Stopping;
  case ServiceState::Registered:
    return false;
  }
  return false;
}

} // namespace

ServiceRegistry::ServiceRegistry() = default;

ServiceRegistry::~ServiceRegistry() = default;

bool ServiceRegistry::can_reset(std::string &error) const {
  StaticLockGuard guard(lock_);
  for (size_t index = 0; index < service_count_; ++index) {
    const auto &service = services_[index];
    if (service.state != ServiceState::Registered &&
        service.state != ServiceState::Stopped) {
      error = "kernel_runtime_requires_stop:" + service.id;
      return false;
    }
  }
  error.clear();
  return true;
}

bool ServiceRegistry::initialize(std::string &error) {
  StaticLockGuard guard(lock_);
  initialized_ = true;
  error.clear();
  return true;
}

void ServiceRegistry::invalidate_availability() {
  required_generation_=0;
  if (++availability_generation_ == 0) {
    ++availability_generation_;
    for (size_t i = 0; i < service_count_; ++i) services_[i].availability_generation = 0;
  }
}

void ServiceRegistry::reset() {
  StaticLockGuard guard(lock_);
  lookup_.clear();
  services_.clear();
  service_count_ = 0;
  invalidate_availability();
  // Keep the revision monotonic across test/product re-composition. Reset is
  // itself a state change and must invalidate consumers holding old snapshots.
  ++revision_;
}

bool ServiceRegistry::register_service(const ServiceDefinition &definition,
                                       std::string &error) {
  StaticLockGuard guard(lock_);
  error.clear();
  if (!initialized_) {
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
  if (definition.dependency_count > 0 && definition.dependencies == nullptr) {
    error = "invalid_service_dependencies";
    return false;
  }

  auto *record = services_.append();
  if (!record) { error = "service_registry_storage_unavailable"; return false; }
  RegisteredService &service = *record;
  service.slot = service_count_;
  if (!service.dependencies.resize(definition.dependency_count)) {
    services_.pop_back(); error = "service_dependency_storage_unavailable"; return false;
  }
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
      services_.pop_back();
      error = "invalid_service_dependency";
      return false;
    }
    for (size_t previous = 0; previous < index; ++previous) {
      if (service.dependencies[previous].id == dependency.id) {
        services_.pop_back();
        error = "duplicate_service_dependency";
        return false;
      }
    }
    service.dependencies[index].id = dependency.id;
    service.dependencies[index].required = dependency.required;
  }
  if (!lookup_.resize(service_count_+1)) {
    services_.pop_back();error="service_lookup_storage_unavailable";return false;
  }
  const auto hash=service_hash(service.id);
  size_t insertion=service_count_;
  while(insertion && lookup_[insertion-1].hash>hash) {
    lookup_[insertion]=lookup_[insertion-1];--insertion;
  }
  lookup_[insertion]={hash,&service};
  service.state = ServiceState::Registered;
  service.revision = ++revision_;
  ++service_count_;
  invalidate_availability();
  return true;
}

bool ServiceRegistry::validate_graph(std::string &error) const {
  StaticLockGuard guard(lock_);
  error.clear();
  if (!initialized_) {
    error = "service_registry_not_initialized";
    return false;
  }
  RegistrationArray<uint8_t> visit_state{true};
  if (!visit_state.resize(service_count_)) { error = "service_graph_storage_unavailable"; return false; }

  const auto service_index = [this](const std::string &id) -> size_t {
    for (size_t index = 0; index < service_count_; ++index) {
      if (services_[index].id == id) {
        return index;
      }
    }
    return service_count_;
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
      if (target == service_count_) {
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
  StaticLockGuard guard(lock_);
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
  StaticLockGuard guard(lock_);
  return transition_locked(service_id, ServiceState::Ready, detail, error);
}

bool ServiceRegistry::mark_degraded(const std::string &service_id,
                                    const char *detail, std::string &error) {
  StaticLockGuard guard(lock_);
  return transition_locked(service_id, ServiceState::Degraded, detail, error);
}

bool ServiceRegistry::mark_failed(const std::string &service_id,
                                  const char *detail, std::string &error) {
  StaticLockGuard guard(lock_);
  return transition_locked(service_id, ServiceState::Failed, detail, error);
}

bool ServiceRegistry::begin_stop(const std::string &service_id,
                                 std::string &error) {
  StaticLockGuard guard(lock_);
  const RegisteredService *service = find_service(service_id);
  if (service == nullptr) {
    error = "service_not_found";
    return false;
  }
  for (size_t index = 0; index < service_count_; ++index) {
    const RegisteredService &candidate = services_[index];
    if (!state_is_available(candidate.state) &&
        candidate.state != ServiceState::Starting &&
        candidate.state != ServiceState::Stopping &&
        candidate.state != ServiceState::Failed) {
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
  StaticLockGuard guard(lock_);
  return transition_locked(service_id, ServiceState::Stopped, detail, error);
}

bool ServiceRegistry::service_registered(const std::string &service_id) const {
  StaticLockGuard guard(lock_);
  return find_service(service_id) != nullptr;
}

bool ServiceRegistry::state(const std::string &service_id,
                            ServiceState &value) const {
  return state(service_id.c_str(), value);
}

bool ServiceRegistry::state(const char *service_id, ServiceState &value) const {
  StaticLockGuard guard(lock_);
  const RegisteredService *service = find_service(service_id);
  if (service == nullptr) return false;
  value = service->state;
  return true;
}

const char *ServiceRegistry::owner_executor(const char *service_id) const {
  StaticLockGuard guard(lock_);
  const auto *service=service_id?find_service(service_id):nullptr;
  return service?service->owner_executor.c_str():nullptr;
}

bool ServiceRegistry::service_available(const std::string &service_id) const {
  return service_available(service_id.c_str());
}

bool ServiceRegistry::service_available(const char *service_id) const {
  StaticLockGuard guard(lock_);
  if (service_id == nullptr || service_id[0] == '\0') {
    return false;
  }
  const RegisteredService *service = find_service(service_id);
  return service != nullptr && effectively_available(*service);
}

#ifndef LUNAROS_REQUIRED_HEALTH_CACHE
#define LUNAROS_REQUIRED_HEALTH_CACHE 1
#endif
bool ServiceRegistry::required_services_ready(std::string *reason) const {
  StaticLockGuard guard(lock_);
  if (reason != nullptr) {
    reason->clear();
  }
  if(LUNAROS_REQUIRED_HEALTH_CACHE&&required_generation_==availability_generation_){
    if(required_blocker_<service_count_&&reason)*reason="required_service_unavailable:"+services_[required_blocker_].id;
    return required_blocker_==service_count_;
  }
  required_blocker_=service_count_;
  for (size_t index = 0; index < service_count_; ++index) {
    const RegisteredService &service = services_[index];
    if (service.criticality == ServiceCriticality::Required &&
        !effectively_available(service)) {
      if (reason != nullptr) {
        *reason = "required_service_unavailable:" + service.id;
      }
      required_blocker_=index;required_generation_=availability_generation_;return false;
    }
  }
  required_generation_=availability_generation_;return true;
}

uint32_t ServiceRegistry::revision() const {
  StaticLockGuard guard(lock_);
  return revision_;
}

uint32_t ServiceRegistry::states_present() const {
  StaticLockGuard guard(lock_);
  uint32_t states = 0;
  for (size_t i = 0; i < service_count_; ++i)
    states |= uint32_t(1) << static_cast<unsigned>(services_[i].state);
  return states;
}
size_t ServiceRegistry::size() const {
  StaticLockGuard guard(lock_); return service_count_;
}
int ServiceRegistry::state_slot(const char *id) const {
  StaticLockGuard guard(lock_);
  const auto *service=find_service(id);
  return service ? static_cast<int>(service->slot) : -1;
}
uint32_t ServiceRegistry::read_states(RegistrationArray<ServiceState> &output) const {
  StaticLockGuard guard(lock_);
  uint32_t present=0;
  output.fill(ServiceState::Registered);
  for(size_t i=0;i<service_count_;++i){output[i]=services_[i].state;present|=uint32_t(1)<<unsigned(output[i]);}
  return present;
}

bool ServiceRegistry::read_lifecycle_facts(ServiceLifecycleFacts &result, std::string &error) const {
  StaticLockGuard guard(lock_);
  if (!result.entries.resize(service_count_)) { error = "service_lifecycle_storage_unavailable"; return false; }
  result.count = service_count_;
  for (size_t i = 0; i < service_count_; ++i) result.entries[i] = {services_[i].id.c_str(), services_[i].state};
  error.clear(); return true;
}
ServiceRegistrySnapshot ServiceRegistry::snapshot() const {
  StaticLockGuard guard(lock_);
  ServiceRegistrySnapshot result;
  if (service_count_) {
#ifdef ESP_PLATFORM
    result.storage.memory = service_count_ && esp_ptr_external_ram(&services_[0]) ? "psram" : "internal";
#else
    result.storage.memory = "host";
#endif
    result.storage.capacity = service_count_;
    result.storage.storage_bytes = (sizeof(RegisteredService) + sizeof(RegisteredService *)) * service_count_+
        sizeof(Lookup)*lookup_.capacity();
    for (size_t i = 0; i < service_count_; ++i)
      result.storage.storage_bytes += sizeof(RegisteredService::Dependency) * services_[i].dependencies.capacity();
  }
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
  result.services.resize(service_count_);
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
    target.dependencies.resize(source.dependency_count);
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
  return find_service(service_id.c_str());
}

ServiceRegistry::RegisteredService *
ServiceRegistry::find_service(const char *service_id) {
  return const_cast<RegisteredService *>(static_cast<const ServiceRegistry *>(this)->find_service(service_id));
}

const ServiceRegistry::RegisteredService *
ServiceRegistry::find_service(const std::string &service_id) const {
  return find_service(service_id.c_str());
}

const ServiceRegistry::RegisteredService *
ServiceRegistry::find_service(const char *service_id) const {
  if (!initialized_) {
    return nullptr;
  }
  if (service_id == nullptr || service_id[0] == '\0') {
    return nullptr;
  }
  const std::string_view name(service_id);
  const auto hash=service_hash(name);
  size_t first=0,last=lookup_.size();
  while(first<last){const size_t middle=first+(last-first)/2;
    if(lookup_[middle].hash<hash)first=middle+1;else last=middle;}
  // Hashes select candidates only. Always compare the owned complete ID,
  // including collisions, before observing lifecycle/availability state.
  for(;first<lookup_.size()&&lookup_[first].hash==hash;++first)
    if(std::string_view(lookup_[first].service->id)==name)return lookup_[first].service;
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
  if (service->state != target) invalidate_availability();
  service->state = target;
  service->detail = detail != nullptr ? detail : "";
  service->revision = ++revision_;
  return true;
}

bool ServiceRegistry::effectively_available(const RegisteredService &service) const {
  // A visiting node is unavailable until all required dependencies pass. This
  // also rejects cycles before graph validation. Registration/reset/state
  // transitions invalidate the whole derived view, including transitive users.
  if (service.availability_generation == availability_generation_) return service.availability == 2;
  service.availability_generation = availability_generation_;
  service.availability = 1;
  if (!state_is_available(service.state)) return false;
  for (size_t index = 0; index < service.dependency_count; ++index) {
    const auto &dependency = service.dependencies[index];
    if (!dependency.required) {
      continue;
    }
    const auto *target = find_service(dependency.id);
    if (target == nullptr || !effectively_available(*target)) {
      return false;
    }
  }
  service.availability = 2;
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

} // namespace lunaredu::lunaros
