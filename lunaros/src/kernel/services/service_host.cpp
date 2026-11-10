#include "kernel/services/service_host_internal.h"

#include <cstring>

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/kernel/service_executor.h"
#endif

namespace lunaredu::lunaros {

namespace {

struct StartInvocation {
  ServiceStartCallback callback = nullptr;
  void *callback_context = nullptr;
  ServiceStartResult result;
};

bool run_start_callback(void *context, std::string &error) {
  auto *invocation = static_cast<StartInvocation *>(context);
  if (invocation == nullptr || invocation->callback == nullptr) {
    error = "invalid_service_start_callback";
    return false;
  }
  invocation->result = invocation->callback(invocation->callback_context);
  error.clear();
  return true;
}

struct StopInvocation {
  ServiceStopCallback callback = nullptr;
  void *callback_context = nullptr;
};

bool run_stop_callback(void *context, std::string &error) {
  auto *invocation = static_cast<StopInvocation *>(context);
  if (invocation == nullptr || invocation->callback == nullptr) {
    error = "invalid_service_stop_callback";
    return false;
  }
  return invocation->callback(invocation->callback_context, error);
}

bool direct_lifecycle_dispatch(const char *, ServiceLifecycleAction action,
                               void *context, std::string &error) {
  if (action == nullptr) {
    error = "invalid_service_lifecycle_action";
    return false;
  }
  return action(context, error);
}

#ifdef ESP_PLATFORM
constexpr const char *kTag = "lunaros.services";

void log_startup_stack(const char *phase, const char *service_id) {
  // ESP-IDF's Xtensa port reports this watermark in bytes. Service startup is
  // deliberately synchronous on the temporary app_main composition task, so
  // this evidence lets products tune that one boot-only stack without adding
  // another resident worker.
  LOGI(kTag, "startup phase=%s service=%s stack_free=%u", phase, service_id,
       static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}
#else
void log_startup_stack(const char *, const char *) {}
#endif

#ifdef ESP_PLATFORM
struct ExecutorLifecycleInvocation {
  ServiceLifecycleAction action = nullptr;
  void *action_context = nullptr;
};

void run_executor_lifecycle(void *context, ServiceOperationResult &result) {
  auto *invocation = static_cast<ExecutorLifecycleInvocation *>(context);
  if (invocation == nullptr || invocation->action == nullptr) {
    result.ok = false;
    result.error = "invalid_service_lifecycle_action";
    return;
  }
  result.ok = invocation->action(invocation->action_context, result.error);
}

bool executor_lifecycle_dispatch(const char *owner_executor,
                                 ServiceLifecycleAction action, void *context,
                                 std::string &error) {
  ExecutorLifecycleInvocation invocation{action, context};
  return invoke_service_operation(owner_executor, run_executor_lifecycle,
                                  &invocation, error);
}
#endif

const ServiceSnapshot *find_snapshot_service(
    const ServiceRegistrySnapshot &snapshot, const char *service_id) {
  if (service_id == nullptr) {
    return nullptr;
  }
  for (size_t index = 0; index < snapshot.service_count; ++index) {
    const ServiceSnapshot &candidate = snapshot.services[index];
    if (candidate.id != nullptr && std::strcmp(candidate.id, service_id) == 0) {
      return &candidate;
    }
  }
  return nullptr;
}

bool waits_for_deferred_dependency(const ServiceRegistrySnapshot &snapshot,
                                   const ServiceSnapshot &service,
                                   size_t depth) {
  if (depth >= snapshot.service_count) {
    return false;
  }
  bool waiting = false;
  for (size_t index = 0; index < service.dependency_count; ++index) {
    const auto &dependency = service.dependencies[index];
    if (!dependency.required || dependency.available) {
      continue;
    }
    const ServiceSnapshot *provider =
        find_snapshot_service(snapshot, dependency.id);
    if (provider == nullptr) {
      return false;
    }
    if (provider->state == ServiceState::Starting) {
      waiting = true;
      continue;
    }
    if (provider->state == ServiceState::Registered &&
        waits_for_deferred_dependency(snapshot, *provider, depth + 1)) {
      waiting = true;
      continue;
    }
    return false;
  }
  return waiting;
}

} // namespace

void ServiceHost::reset() {
  nodes_ = {};
  node_count_ = 0;
}

bool ServiceHost::install(const ServiceNode &node, std::string &error) {
  error.clear();
  const ServiceDefinition &definition = node.definition;
  if (definition.id == nullptr || definition.id[0] == '\0' ||
      node.start == nullptr) {
    error = "invalid_service_node";
    return false;
  }
  if (find_node(definition.id) != nullptr) {
    error = "service_node_already_installed";
    return false;
  }
  if (node_count_ >= nodes_.size()) {
    error = "service_node_registry_full";
    return false;
  }
  if (!registry_.register_service(definition, error)) {
    return false;
  }
  nodes_[node_count_++] = node;
  return true;
}

bool ServiceHost::start(const std::string &service_id, std::string &error) {
  error.clear();
  const ServiceNode *node = find_node(service_id);
  if (node == nullptr) {
    std::string transition_error;
    if (registry_.begin_start(service_id, transition_error)) {
      (void)registry_.mark_failed(service_id, "driver_not_registered",
                                  transition_error);
    }
    error = "service_driver_not_registered:" + service_id;
    return false;
  }
  if (!registry_.begin_start(service_id, error)) {
    return false;
  }

  log_startup_stack("begin", service_id.c_str());
  StartInvocation invocation{node->start, node->context, {}};
  std::string dispatch_error;
  if (!dispatch(*node, run_start_callback, &invocation, dispatch_error)) {
    std::string transition_error;
    (void)registry_.mark_failed(service_id, "lifecycle_dispatch_failed",
                                transition_error);
    error = "service_lifecycle_dispatch_failed:" + service_id;
    if (!dispatch_error.empty()) {
      error += ":" + dispatch_error;
    }
    return false;
  }
  const ServiceStartResult &result = invocation.result;
  log_startup_stack("end", service_id.c_str());
  const char *detail = result.detail.c_str();
  switch (result.disposition) {
  case ServiceStartDisposition::Ready:
    return registry_.mark_ready(service_id, detail, error);
  case ServiceStartDisposition::Degraded:
    return registry_.mark_degraded(service_id, detail, error);
  case ServiceStartDisposition::Deferred:
    return true;
  case ServiceStartDisposition::Failed:
    if (!registry_.mark_failed(service_id, detail, error)) {
      return false;
    }
    error = "service_start_failed:" + service_id;
    if (!result.detail.empty()) {
      error += ":" + result.detail;
    }
    return false;
  }
  error = "invalid_service_start_result:" + service_id;
  return false;
}

bool ServiceHost::activate(const std::string &service_id,
                           std::string &error) {
  error.clear();
  const ServiceNode *node = find_node(service_id);
  if (node == nullptr || node->activate == nullptr) {
    error = "service_activation_not_registered:" + service_id;
    return false;
  }

  const ServiceRegistrySnapshot before = registry_.snapshot();
  const ServiceSnapshot *service = nullptr;
  for (size_t index = 0; index < before.service_count; ++index) {
    if (before.services[index].id != nullptr &&
        service_id == before.services[index].id) {
      service = &before.services[index];
      break;
    }
  }
  if (service == nullptr) {
    error = "service_not_found";
    return false;
  }
  if (service->state != ServiceState::Starting) {
    error = "service_not_deferred:" + service_id;
    return false;
  }

  log_startup_stack("activate_begin", service_id.c_str());
  StartInvocation invocation{node->activate, node->context, {}};
  std::string dispatch_error;
  if (!dispatch(*node, run_start_callback, &invocation, dispatch_error)) {
    error = "service_activation_dispatch_failed:" + service_id;
    if (!dispatch_error.empty()) {
      error += ":" + dispatch_error;
    }
    return false;
  }
  const ServiceStartResult &result = invocation.result;
  log_startup_stack("activate_end", service_id.c_str());
  const char *detail = result.detail.c_str();
  switch (result.disposition) {
  case ServiceStartDisposition::Ready:
    if (!registry_.mark_ready(service_id, detail, error)) {
      return false;
    }
    break;
  case ServiceStartDisposition::Degraded:
    if (!registry_.mark_degraded(service_id, detail, error)) {
      return false;
    }
    break;
  case ServiceStartDisposition::Deferred:
    return true;
  case ServiceStartDisposition::Failed:
    if (!registry_.mark_failed(service_id, detail, error)) {
      return false;
    }
    error = "service_activation_failed:" + service_id;
    if (!result.detail.empty()) {
      error += ":" + result.detail;
    }
    return false;
  }

  // A deferred service becoming available may unblock other registered
  // services. ServiceHost owns that transition and runs their start callbacks
  // on the current owner executor.
  return start_all(error);
}

bool ServiceHost::start_all(std::string &error) {
  if (!registry_.validate_graph(error)) {
    return false;
  }

  bool progressed = false;
  do {
    progressed = false;
    const ServiceRegistrySnapshot before = registry_.snapshot();
    for (size_t index = 0; index < before.service_count; ++index) {
      const ServiceSnapshot &service = before.services[index];
      if (service.state != ServiceState::Registered) {
        continue;
      }
      bool dependencies_ready = true;
      for (size_t dependency_index = 0;
           dependency_index < service.dependency_count; ++dependency_index) {
        const auto &dependency = service.dependencies[dependency_index];
        if (dependency.required && !dependency.available) {
          dependencies_ready = false;
          break;
        }
      }
      if (!dependencies_ready) {
        continue;
      }

      std::string start_error;
      if (!start(service.id, start_error)) {
        if (service.criticality == ServiceCriticality::Required) {
          error = start_error;
          return false;
        }
      }
      progressed = true;
    }
  } while (progressed);

  const ServiceRegistrySnapshot after = registry_.snapshot();
  for (size_t index = 0; index < after.service_count; ++index) {
    const ServiceSnapshot &service = after.services[index];
    if (service.state == ServiceState::Registered &&
        service.criticality == ServiceCriticality::Required &&
        !waits_for_deferred_dependency(after, service, 0)) {
      error = "required_service_start_blocked:" + std::string(service.id);
      return false;
    }
  }
  error.clear();
  return true;
}

bool ServiceHost::report_ready(const std::string &service_id,
                               const char *detail, std::string &error) {
  if (!registry_.mark_ready(service_id, detail, error)) {
    return false;
  }
  return start_all(error);
}

bool ServiceHost::report_degraded(const std::string &service_id,
                                  const char *detail, std::string &error) {
  if (!registry_.mark_degraded(service_id, detail, error)) {
    return false;
  }
  return start_all(error);
}

bool ServiceHost::report_failed(const std::string &service_id,
                                const char *detail, std::string &error) {
  return registry_.mark_failed(service_id, detail, error);
}

bool ServiceHost::stop_all(std::string &error) {
  bool progressed = false;
  do {
    progressed = false;
    const ServiceRegistrySnapshot snapshot = registry_.snapshot();
    for (size_t offset = 0; offset < snapshot.service_count; ++offset) {
      const size_t index = snapshot.service_count - offset - 1;
      const ServiceSnapshot &service = snapshot.services[index];
      if (service.state != ServiceState::Ready &&
          service.state != ServiceState::Degraded &&
          service.state != ServiceState::Starting &&
          service.state != ServiceState::Failed) {
        continue;
      }

      std::string stop_error;
      if (!registry_.begin_stop(service.id, stop_error)) {
        if (stop_error.rfind("service_has_active_dependent:", 0) == 0) {
          continue;
        }
        error = stop_error;
        return false;
      }
      const ServiceNode *node = find_node(service.id);
      StopInvocation invocation{node != nullptr ? node->stop : nullptr,
                                node != nullptr ? node->context : nullptr};
      if (node != nullptr && node->stop != nullptr &&
          !dispatch(*node, run_stop_callback, &invocation, stop_error)) {
        std::string transition_error;
        (void)registry_.mark_failed(service.id, "stop_failed",
                                    transition_error);
        error = "service_stop_failed:" + std::string(service.id);
        if (!stop_error.empty()) {
          error += ":" + stop_error;
        }
        return false;
      }
      if (!registry_.mark_stopped(service.id, "stopped", stop_error)) {
        error = stop_error;
        return false;
      }
      progressed = true;
    }
  } while (progressed);

  const ServiceRegistrySnapshot after = registry_.snapshot();
  for (size_t index = 0; index < after.service_count; ++index) {
    const ServiceState state = after.services[index].state;
    if (state == ServiceState::Ready || state == ServiceState::Degraded ||
        state == ServiceState::Starting || state == ServiceState::Failed) {
      error = "service_stop_blocked:" + std::string(after.services[index].id);
      return false;
    }
  }
  error.clear();
  return true;
}

const ServiceNode *ServiceHost::find_node(
    const std::string &service_id) const {
  for (size_t index = 0; index < node_count_; ++index) {
    if (nodes_[index].definition.id != nullptr &&
        service_id == nodes_[index].definition.id) {
      return &nodes_[index];
    }
  }
  return nullptr;
}

bool ServiceHost::dispatch(const ServiceNode &node,
                           ServiceLifecycleAction action, void *context,
                           std::string &error) const {
  ServiceLifecycleDispatcher dispatcher =
      dispatcher_ != nullptr ? dispatcher_ : direct_lifecycle_dispatch;
  const char *owner = node.definition.owner_executor != nullptr
                          ? node.definition.owner_executor
                          : "lunar.main";
  return dispatcher(owner, action, context, error);
}

ServiceHost &service_host_internal() {
#ifdef ESP_PLATFORM
  static ServiceHost host(service_registry_internal(),
                          executor_lifecycle_dispatch);
#else
  static ServiceHost host(service_registry_internal());
#endif
  return host;
}

void reset_service_host_internal() { service_host_internal().reset(); }

bool start_all_services(std::string &error) {
  return service_host_internal().start_all(error);
}

bool activate_service(const char *service_id, std::string &error) {
  if (service_id == nullptr) {
    error = "invalid_service_id";
    return false;
  }
  return service_host_internal().activate(service_id, error);
}

bool report_service_ready(const char *service_id, const char *detail,
                          std::string &error) {
  if (service_id == nullptr) {
    error = "invalid_service_id";
    return false;
  }
  return service_host_internal().report_ready(service_id, detail, error);
}

bool report_service_degraded(const char *service_id, const char *detail,
                             std::string &error) {
  if (service_id == nullptr) {
    error = "invalid_service_id";
    return false;
  }
  return service_host_internal().report_degraded(service_id, detail, error);
}

bool report_service_failed(const char *service_id, const char *detail,
                           std::string &error) {
  if (service_id == nullptr) {
    error = "invalid_service_id";
    return false;
  }
  return service_host_internal().report_failed(service_id, detail, error);
}

bool stop_all_services(std::string &error) {
  return service_host_internal().stop_all(error);
}

} // namespace lunaredu::lunaros
