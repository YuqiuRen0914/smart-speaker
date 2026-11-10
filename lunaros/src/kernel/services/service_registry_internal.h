#pragma once

#include <array>
#include <atomic>
#include <string>

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

#include "lunaredu_lunaros/kernel/service_registry.h"

namespace lunaredu::lunaros {

class ServiceRegistry {
public:
  ServiceRegistry();
  bool initialize(std::string &error);
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
  bool service_available(const std::string &service_id) const;
  bool required_services_ready(std::string *reason = nullptr) const;
  ServiceRegistrySnapshot snapshot() const;

private:
  class Guard {
  public:
#ifdef ESP_PLATFORM
    explicit Guard(SemaphoreHandle_t lock) : lock_(lock) {
      if (lock_ != nullptr) {
        (void)xSemaphoreTake(lock_, portMAX_DELAY);
      }
    }
    ~Guard() {
      if (lock_ != nullptr) {
        (void)xSemaphoreGive(lock_);
      }
    }
#else
    explicit Guard(std::atomic_flag &lock) : lock_(lock) {
      while (lock_.test_and_set(std::memory_order_acquire)) {
      }
    }
    ~Guard() { lock_.clear(std::memory_order_release); }
#endif
    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;

  private:
#ifdef ESP_PLATFORM
    SemaphoreHandle_t lock_ = nullptr;
#else
    std::atomic_flag &lock_;
#endif
  };

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
    std::array<Dependency, kServiceDependencyCapacity> dependencies;
  };

  RegisteredService *find_service(const std::string &service_id);
  const RegisteredService *find_service(const std::string &service_id) const;
  bool transition_locked(const std::string &service_id, ServiceState target,
                         const char *detail, std::string &error);
  bool dependencies_available(const RegisteredService &service,
                              std::string &reason) const;
  bool effectively_available(const RegisteredService &service) const;
  bool effectively_available(const RegisteredService &service,
                             std::array<int8_t, kServiceCapacity> &cache) const;

#ifdef ESP_PLATFORM
  // Registrations are immutable after product installation, and snapshots
  // expose stable c_str() pointers into these records. Allocate this
  // non-trivial array once in PSRAM and never relocate or free it during boot.
  RegisteredService *services_ = nullptr;
#else
  std::array<RegisteredService, kServiceCapacity> owned_services_{};
  RegisteredService *services_ = owned_services_.data();
#endif
  size_t service_count_ = 0;
  uint32_t revision_ = 0;
#ifdef ESP_PLATFORM
  mutable StaticSemaphore_t lock_storage_{};
  mutable SemaphoreHandle_t lock_ = nullptr;
#else
  mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
#endif
};

ServiceRegistry &service_registry_internal();
void reset_service_registry_internal();

} // namespace lunaredu::lunaros
