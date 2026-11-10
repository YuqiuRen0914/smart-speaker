#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#else
using QueueHandle_t = void *;
#endif

#include "kernel/service_executor.h"
#include "kernel/registration_storage.h"

namespace lunaredu::lunaros {

struct ServiceExecutorState {
  std::atomic_flag admission_lock = ATOMIC_FLAG_INIT;
  char id[24] = {0};
  char owner_service_id[32] = {0};
  QueueHandle_t queue = nullptr;
  std::atomic<uintptr_t> task_handle{0};
  std::atomic<uintptr_t> waiting_for{0};
  std::atomic<uint32_t> pending_calls{0};
  std::atomic<bool> retiring{false};
  ServiceExecutorActivation activation{};
  uint8_t queue_depth = 0;
};

class ServiceExecutorRegistry {
public:
  bool setup(std::initializer_list<ServiceExecutorDefinition> definitions,
             std::string *error) { return setup_range(definitions.begin(), definitions.size(), error); }
  bool setup_range(const ServiceExecutorDefinition *, size_t, std::string *);
  bool can_reset(std::string *error) const;
  bool reset(std::string *error);
  bool register_executor(const ServiceExecutorDefinition &definition,
                         std::string *error);
  bool bind(const char *executor_id, std::string *error);
  // Closes foreign admission on the owner; false preserves the owner to drain.
  bool prepare_retirement(const char *executor_id, std::string *error);
  bool unbind(const char *executor_id, std::string *error);
  bool pump(const char *executor_id, uint32_t wait_timeout_ms,
            std::string *error);
  size_t snapshot(ServiceExecutorSnapshot *output, size_t capacity) const;
  uint32_t pending_calls(const char *executor_id) const;
  bool current_task_owns() const;
  bool current_task_owns(const char *id) const;
  bool invoke(const char *executor_id, ServiceOperation operation,
              void *context, std::string &error,
              uint32_t admission_timeout_ms = 100);
  bool submit(const char *, std::unique_ptr<ServiceRequest>, ServiceCall &,
              std::string &, uint32_t,bool queue_only=false);
  bool submit_retained(const char *, std::unique_ptr<ServiceRequest> &, ServiceCall &,
              std::string &, uint32_t,bool queue_only);
  ExecutorReadiness request_activation(const char *);
  bool wait(ServiceCall &, uint32_t, std::string &);

private:
  ServiceExecutorState *find(const char *id);
  const ServiceExecutorState *find(const char *id) const;

  RegistrationTable<ServiceExecutorState> executors_;
  size_t executor_count_ = 0;
};

} // namespace lunaredu::lunaros
