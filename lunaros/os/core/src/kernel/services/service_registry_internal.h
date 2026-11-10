#pragma once

#include <vector>
#include "kernel/registration_storage.h"
#include <string>
#include "kernel/service.h"
#include "kernel/static_mutex.h"

namespace lunaredu::lunaros {

struct ServiceLifecycleFacts {
  struct Entry { const char *id=nullptr; ServiceState state=ServiceState::Registered; };
  RegistrationArray<Entry> entries;
  size_t count=0;
};
class ServiceRegistry {
public:
  ServiceRegistry();
  ~ServiceRegistry();
  ServiceRegistry(const ServiceRegistry &) = delete;
  ServiceRegistry &operator=(const ServiceRegistry &) = delete;
  bool initialize(std::string &error);
  bool can_reset(std::string &error) const;
  void reset();
  bool register_service(const ServiceDefinition &definition,
                        std::string &error);
  bool validate_graph(std::string &error) const;
  bool begin_start(const std::string &service_id, std::string &error);
  bool mark_ready(const std::string &service_id, const char *detail,
                  std::string &error);
  bool mark_degraded(const std::string &service_id, const char *detail,
                     std::string &error);
  bool mark_failed(const std::string &service_id, const char *detail,
                   std::string &error);
  bool begin_stop(const std::string &service_id, std::string &error);
  bool mark_stopped(const std::string &service_id, const char *detail,
                    std::string &error);
  bool service_registered(const std::string &service_id) const;
  bool state(const std::string &service_id, ServiceState &value) const;
  bool state(const char *service_id, ServiceState &value) const;
  const char *owner_executor(const char *service_id) const;
  bool service_available(const std::string &service_id) const;
  bool service_available(const char *service_id) const;
  bool required_services_ready(std::string *reason = nullptr) const;
  uint32_t revision() const;
  // One bounded, allocation-free observation for lifecycle admission scans.
  uint32_t states_present() const;
  int state_slot(const char *id) const;
  // Owner reconciliation observes the whole bounded registry under one lock.
  uint32_t read_states(RegistrationArray<ServiceState> &) const;
  size_t size() const;
  ServiceRegistrySnapshot snapshot() const;
  bool read_lifecycle_facts(ServiceLifecycleFacts &, std::string &error) const;

private:
  struct RegisteredService {
    std::string id;
    std::string title;
    std::string owner_executor;
    ServiceCriticality criticality = ServiceCriticality::Optional;
    ServiceState state = ServiceState::Registered;
    std::string detail;
    uint32_t revision = 0;
    size_t dependency_count = 0;
    struct Dependency {
      std::string id;
      bool required = true;
    };
    RegistrationArray<Dependency> dependencies{true};
    size_t slot = 0;
    mutable uint32_t availability_generation = 0;
    mutable uint8_t availability = 0;
  };

  RegisteredService *find_service(const std::string &service_id);
  const RegisteredService *find_service(const std::string &service_id) const;
  RegisteredService *find_service(const char *service_id);
  const RegisteredService *find_service(const char *service_id) const;
  bool transition_locked(const std::string &service_id, ServiceState target,
                         const char *detail, std::string &error);
  bool dependencies_available(const RegisteredService &service,
                              std::string &reason) const;
  bool effectively_available(const RegisteredService &service) const;
  void invalidate_availability();
  RegistrationTable<RegisteredService> services_{true};
  // Compact sorted lookup stays with cold registry storage. A read touches
  // only matching records, rather than chasing every PSRAM record/string.
  struct Lookup { uint32_t hash=0; RegisteredService *service=nullptr; };
  RegistrationArray<Lookup> lookup_{true};
#ifdef ESP_PLATFORM
  bool initialized_ = false;
#else
  bool initialized_ = true;
#endif
  size_t service_count_ = 0;
  uint32_t revision_ = 0;
  uint32_t availability_generation_ = 1;
  mutable uint32_t required_generation_ = 0;
  mutable size_t required_blocker_ = 0;
  mutable StaticMutex lock_;
};

} // namespace lunaredu::lunaros
