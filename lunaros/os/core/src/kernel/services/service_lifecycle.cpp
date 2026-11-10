#include "kernel/services/service_host_internal.h"
#include "kernel/runtime.h"

#include <cstring>

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>
#include "kernel/service_executor.h"
#include "kernel/memory_status.h"
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
  ServiceStopStep step = nullptr;
  ServiceStopResult result;
};
bool run_stop_callback(void *context, std::string &error) {
  auto *invocation = static_cast<StopInvocation *>(context);
  if (invocation && invocation->step) {
    invocation->result = invocation->step(invocation->callback_context);
    return true;
  }
  if (invocation == nullptr || invocation->callback == nullptr) {
    error = "invalid_service_stop_callback";
    return false;
  }
  return invocation->callback(invocation->callback_context, error);
}
#ifdef ESP_PLATFORM
constexpr const char *kTag = "lunaros.services";

void log_startup_stack(const char *phase, const char *service_id) {
  // ESP-IDF's Xtensa port reports this watermark in bytes. Service startup is
  // deliberately synchronous on the temporary app_main composition task, so
  // this evidence lets products tune that one boot-only stack without adding
  // another resident worker.
  ESP_LOGI(kTag, "startup phase=%s service=%s stack_free=%u", phase, service_id,
       static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}
#else
void log_startup_stack(const char *, const char *) {}
#endif
}

bool ServiceHost::publish_start_result(const std::string &id,const ServiceStartResult &result,
    bool activation,std::string &error) {
  switch(result.disposition) {
    case ServiceStartDisposition::Ready: return registry_.mark_ready(id,result.detail.c_str(),error);
    case ServiceStartDisposition::Degraded: return registry_.mark_degraded(id,result.detail.c_str(),error);
    case ServiceStartDisposition::Deferred: return true;
    case ServiceStartDisposition::Failed:
      if(!registry_.mark_failed(id,result.detail.c_str(),error)) return false;
      error=(activation?"service_activation_failed:":"service_start_failed:")+id;
      if(!result.detail.empty()) error+=":"+result.detail;
      return false;
  }
  error="invalid_service_start_result:"+id; return false;
}

bool ServiceHost::activate(const std::string &service_id,
                           std::string &error) {
  error.clear();
  const ServiceNode *node = find_node(service_id);
  if (node == nullptr || node->activate == nullptr) {
    error = "service_activation_not_registered:" + service_id;
    return false;
  }

  ServiceState state;
  if (!registry_.state(service_id,state)) {
    error="service_not_found"; return false;
  }
  if (state != ServiceState::Starting) {
    error = "service_not_deferred:" + service_id;
    return false;
  }
  if (!execution_wanted(*node)) {
    error = "service_execution_not_demanded:" + service_id;
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
  if (!publish_start_result(service_id,result,true,error)) return false;
  if(result.disposition==ServiceStartDisposition::Deferred) return true;

  // A deferred service becoming available may unblock other registered
  // services. ServiceHost owns that transition and runs their start callbacks
  // on the current owner executor.
  return start_all(error);
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
  if (!execution_wanted(*node)) {
    error = "service_execution_not_demanded:" + service_id; return false;
  }
  // Failed startup can leave driver-owned resources behind. Finish its stop
  // callback before admitting a new start attempt.
  ServiceState previous = ServiceState::Registered;
  if (registry_.state(service_id, previous) &&
      previous == ServiceState::Failed &&
      !stop_one(service_id, error)) {
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
    const std::string detail = dispatch_error.empty()
                                   ? "lifecycle_dispatch_failed"
                                   : "lifecycle_dispatch_failed:" + dispatch_error;
    (void)registry_.mark_failed(service_id, detail.c_str(), transition_error);
    error = "service_lifecycle_dispatch_failed:" + service_id;
    if (!dispatch_error.empty()) {
      error += ":" + dispatch_error;
    }
    return false;
  }
  const ServiceStartResult &result = invocation.result;
  log_startup_stack("end", service_id.c_str());
#ifdef ESP_PLATFORM
  system::record_boot_memory_checkpoint(service_id.c_str());
#endif
  return publish_start_result(service_id,result,false,error);
}

bool ServiceHost::restart(const std::string &service_id, std::string &error) {
  error.clear();
  if (!prepared_ && !prepare(error)) return false;
  ServiceState state = ServiceState::Registered;
  if (!registry_.state(service_id, state)) {
    error = "service_not_found";
    return false;
  }
  if (state != ServiceState::Registered &&
      state != ServiceState::Stopped &&
      !stop_one(service_id, error)) {
    return false;
  }
  if (!start(service_id, error)) {
    const std::string startup_error = error;
    ServiceState after = ServiceState::Registered;
    if (registry_.state(service_id, after) &&
        after == ServiceState::Failed) {
      std::string cleanup_error;
      if (!stop_one(service_id, cleanup_error)) {
        error = startup_error + ";service_cleanup_failed:" + cleanup_error;
        return false;
      }
    }
    error = startup_error;
    return false;
  }
  return start_all(error);
}

bool ServiceHost::stop_all(std::string &error) {
  RegistrationArray<uint8_t> attempted;
  if (!attempted.resize(registry_.size())) { error = "service_stop_storage_unavailable"; return false; }
  std::string first_error;
  bool progressed = false;
  ServiceLifecycleFacts snapshot;
  do {
    progressed = false;
    if (!registry_.read_lifecycle_facts(snapshot, error)) return false;
    for (size_t offset = 0; offset < snapshot.count; ++offset) {
      const size_t index = snapshot.count - offset - 1;
      const auto &service = snapshot.entries[index];
      if (attempted[index]) continue;
      if (service.state != ServiceState::Ready &&
          service.state != ServiceState::Degraded &&
          service.state != ServiceState::Starting &&
          service.state != ServiceState::Failed &&
          service.state != ServiceState::Stopping) {
        continue;
      }

      std::string stop_error;
      if (!stop_one(service.id, stop_error)) {
        if (stop_error.rfind("service_has_active_dependent:", 0) == 0) {
          continue;
        }
        attempted[index] = true;
        if (first_error.empty()) first_error = stop_error;
        continue;
      }
      attempted[index] = true;
      progressed = true;
    }
  } while (progressed);

  if (!registry_.read_lifecycle_facts(snapshot, error)) return false;
  const auto &after = snapshot;
  for (size_t index = 0; index < after.count; ++index) {
    const ServiceState state = after.entries[index].state;
    if (state == ServiceState::Ready || state == ServiceState::Degraded ||
        state == ServiceState::Starting || state == ServiceState::Failed ||
        state == ServiceState::Stopping) {
      error = first_error.empty()
                  ? "service_stop_blocked:" + std::string(after.entries[index].id)
                  : first_error;
      return false;
    }
  }
  error.clear();
  return true;
}

bool ServiceHost::stop_one(const std::string &service_id,
                           std::string &error) {
  error.clear();
  const ServiceNode *node = find_node(service_id);
  if (node && node->before_stop) {
    StopInvocation preflight{};
    preflight.callback = node->before_stop;
    preflight.callback_context = node->context;
    if (!dispatch(*node, run_stop_callback, &preflight, error)) {
      if (error.empty()) error = "service_stop_not_admitted:" + service_id;
      return false;
    }
  }
  if (!registry_.begin_stop(service_id, error)) return false;
  StopInvocation invocation{node != nullptr ? node->stop : nullptr,
                            node != nullptr ? node->context : nullptr,
                            node != nullptr ? node->stop_step : nullptr, {}};
  if (node != nullptr && (node->stop != nullptr || node->stop_step != nullptr) &&
      !dispatch(*node, run_stop_callback, &invocation, error)) {
    std::string transition_error;
    const std::string detail = error;
    const std::string state_detail = detail.empty()
                                         ? "stop_failed"
                                         : "stop_failed:" + detail;
    (void)registry_.mark_failed(service_id, state_detail.c_str(),
                                transition_error);
    error = "service_stop_failed:" + service_id;
    if (!detail.empty()) error += ":" + detail;
    return false;
  }
  if (node && node->stop_step) {
    if (invocation.result.disposition == ServiceStopDisposition::Pending) {
      error = "service_stop_pending:" + service_id + ":" + invocation.result.detail;
      return false;
    }
    if (invocation.result.disposition == ServiceStopDisposition::Failed) {
      std::string ignored;
      (void)registry_.mark_failed(service_id, invocation.result.detail.c_str(), ignored);
      error = "service_stop_failed:" + service_id + ":" + invocation.result.detail;
      return false;
    }
  }
  return registry_.mark_stopped(service_id, "stopped", error);
}

} // namespace lunaredu::lunaros
