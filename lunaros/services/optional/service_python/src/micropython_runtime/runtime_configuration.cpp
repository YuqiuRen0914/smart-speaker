#include "device_api/adapters/application_projection.h"
#include "lunaros_micropython_runtime_config.h"

#include "lunaros_micropython_hardware_bindings.h"
#include "lunaros_micropython_runtime.h"
#include "lunaros_micropython_app_backend.h"
#include "kernel/service_executor.h"
#include "kernel/managed_task.h"
#include "kernel/runtime.h"
#include "device_api/device_api_context.h"


#include "app/lifecycle/app_controller.h"

#include <atomic>

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#endif

#ifndef ESP_PLATFORM
#include <chrono>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <thread>
#endif
#endif

namespace lunaredu::micropython {
namespace {


std::atomic<RuntimeConfig *> g_runtime_config{nullptr};
std::atomic<decltype(RuntimeControlSurface::set_available)> g_control_available{nullptr};
std::atomic<decltype(RuntimeControlSurface::foreground_application)> g_control_foreground{nullptr};
std::atomic<bool> g_runtime_start_in_progress{false};
std::atomic<bool> g_runtime_stopping{false};
std::atomic<bool> g_runtime_preparing{false};
bool set_runtime_control_available(bool available, std::string &error);

} // namespace

namespace {
uint64_t startup_time_ms() {
#ifdef ESP_PLATFORM
  return uint64_t(esp_timer_get_time()/1000);
#else
  return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
void wait_for_startup(bool preparation=false) {
  const uint64_t started=startup_time_ms();
  while((g_runtime_start_in_progress.load(std::memory_order_acquire) || (preparation && g_runtime_preparing.load(std::memory_order_acquire))) && startup_time_ms()-started<5000) {
#ifdef ESP_PLATFORM
    vTaskDelay(1);
#elif defined(_WIN32)
    Sleep(1);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
  }
}
} // namespace
bool configure_runtime(RuntimeConfig &config,std::string &error) {
  error.clear();
  if (!core_enabled()) return true;
  setup_micropython_lesson_modules(config.machine.call!=nullptr);
  g_runtime_stopping.store(false, std::memory_order_release);
  if (config.install_hardware_bindings) {
    setup_micropython_hardware_bindings();
  }
  // Product service configuration has static lifetime and is installed before
  // application/API tasks can request the VM.
  g_runtime_config = &config;
  if (!config.start_on_demand && !ensure_runtime_started(error)) {
    return false;
  }
  // Availability belongs to the configured service, not to a particular VM.
  // The projection is also the dormant VM's startup and retry entrypoint.
  // A transient VM failure must leave status/file operations and retries usable.
  if (!set_runtime_control_available(true, error)) {
    if (error.empty()) error = "projection_activation_failed";
    return false;
  }
  return true;
}

bool stop_configured_runtime(RuntimeConfig &config, std::string &error) {
  error.clear();
  if (!core_enabled()) return true;
  g_runtime_stopping.store(true, std::memory_order_release);
  wait_for_startup(true);
  if (g_runtime_start_in_progress.load(std::memory_order_acquire) || g_runtime_preparing.load(std::memory_order_acquire)) {
    error = "python_runtime_start_pending";
    return false;
  }
  if (!config.owner_device || !micropython_shutdown_runtime(config.owner_device->runtime, error))
    return false;
  if (!set_runtime_control_available(false, error)) return false;
  g_runtime_config = nullptr;
  return true;
}

void configure_runtime_control_surface(RuntimeControlSurface control) {
  g_control_available.store(control.set_available, std::memory_order_release);
  g_control_foreground.store(control.foreground_application, std::memory_order_release);
}
namespace {
bool set_runtime_control_available(bool available, std::string &error) {
  const auto notify = g_control_available.load(std::memory_order_acquire);
  error.clear();
  return !notify || notify(available, error);
}
} // namespace
std::string runtime_foreground_application(const lunaros::DeviceApiContext &device) {
  const auto read = g_control_foreground.load(std::memory_order_acquire);
  const char *id = read ? read(device) : nullptr;
  return id ? id : "";
}
bool core_enabled() {
#if defined(CONFIG_LUNAROS_ENABLE_MICROPYTHON)
  return CONFIG_LUNAROS_ENABLE_MICROPYTHON != 0;
#else
  return false;
#endif
}

bool hardware_binding_access_allowed(const char *capability) {
  // Background Agent programs use the scoped lunar.call facade exclusively.
  if (micropython_background_active()) return false;
  if (!capability || !*capability) return false;
  const auto *config = g_runtime_config.load(std::memory_order_acquire);
  return !config || !config->hardware_access || config->hardware_access(capability);
}

bool runtime_configured() {
  const auto *config = g_runtime_config.load(std::memory_order_acquire);
  return core_enabled() && !g_runtime_stopping.load(std::memory_order_acquire) &&
      config != nullptr && config->owner_device != nullptr;
}

bool transition_foreground(bool active, std::string &error) {
  error.clear();
  if (!runtime_configured()) { error = "runtime_service_not_configured"; return false; }
  const auto *config = g_runtime_config.load(std::memory_order_acquire);
  if (!config) { error = "runtime_service_not_configured"; return false; }
  return !config->foreground_transition || config->foreground_transition(config->foreground_context, active, error);
}

const RobotMotionPort *runtime_robot_motion_port() {
  const auto *config = g_runtime_config.load(std::memory_order_acquire);
  return config != nullptr ? &config->robot_motion : nullptr;
}
const MachinePort *runtime_machine_port() {
  const auto *config=g_runtime_config.load(std::memory_order_acquire);
  return config&&config->machine.call?&config->machine:nullptr;
}

lunaredu::lunaros::DeviceApiContext *runtime_device_context() {
  const auto *config = g_runtime_config.load(std::memory_order_acquire);
  return config != nullptr ? config->owner_device : nullptr;
}

lunaredu::app::CapabilityRegistry *runtime_application_capabilities() {
  auto *device = runtime_device_context();
  return device ? device_api_application_capabilities(*device) : nullptr;
}

namespace {
bool ensure_runtime_started_impl(std::string &error, MicroPythonStartupClaim *claim) {
  if (claim) *claim = {};
  error.clear();
  if (!core_enabled()) { error = "micropython_explicitly_disabled"; return false; }
  if (g_runtime_stopping.load(std::memory_order_acquire)) { error = "python_runtime_stopping"; return false; }
  const auto preparation = micropython_runtime_startup_claim();
  if (!preparation) { error = "python_startup_revisions_exhausted"; return false; }
  if (micropython_runtime_status().initialized) {
    return true;
  }
  const auto *config = g_runtime_config.load(std::memory_order_acquire);
  if (config == nullptr || config->owner_device == nullptr) {
    error = "runtime_service_not_configured";
    return false;
  }

  bool expected = false;
  if (g_runtime_start_in_progress.compare_exchange_strong(
          expected, true, std::memory_order_acq_rel)) {
    // A stop can close admission between the first check and this claim.
    if (g_runtime_stopping.load(std::memory_order_acquire)) {
      g_runtime_start_in_progress.store(false, std::memory_order_release);
      error = "python_runtime_stopping";
      return false;
    }
    if (claim) *claim = preparation;
    setup_micropython_runtime(config->owner_device->runtime);
    g_runtime_start_in_progress.store(false, std::memory_order_release);
  } else {
    wait_for_startup();
  }
  const MicroPythonRuntimeStatus status = micropython_runtime_status();
  if (status.initialized) {
    return true;
  }
  error = status.last_error.empty() ? "vm_initialization_failed"
                                    : status.last_error;
  return false;
}
} // namespace

bool ensure_runtime_started(std::string &error) {
  return ensure_runtime_started_impl(error, nullptr);
}
bool ensure_runtime_started(std::string &error, MicroPythonStartupClaim &claim) {
  return ensure_runtime_started_impl(error, &claim);
}

bool prepare_runtime_async(std::string &error) {
  error.clear();
  if(!core_enabled() || g_runtime_stopping.load(std::memory_order_acquire)){error="python_runtime_unavailable";return false;}
  // Protect a foreground preparation immediately, before its transient owner
  // starts. The VM may still look idle while that owner is awaiting admission.
  if (!micropython_runtime_startup_claim()) { error="python_startup_revisions_exhausted"; return false; }
  if(micropython_runtime_status().initialized)return true;
  auto *device=runtime_device_context();if(!device){error="runtime_service_not_configured";return false;}
#ifdef ESP_PLATFORM
  bool expected=false;if(!g_runtime_preparing.compare_exchange_strong(expected,true,std::memory_order_acq_rel))return true;
  const lunaros::ManagedTaskDefinition definition{.id="python_prepare",.owner_service_id="runtime.micropython",.role="vm_startup",.stack_bytes=8192,.priority=1,.core_affinity=lunaros::kManagedTaskDefaultCore,.lifetime=lunaros::ManagedTaskLifetime::Transient,.memory=lunaros::ManagedTaskMemory::Internal,.allow_internal_fallback=false};
  const bool created=lunaros::create_managed_task(device->runtime,definition,[](void *){std::string error;(void)ensure_runtime_started(error);g_runtime_preparing.store(false,std::memory_order_release);},nullptr,nullptr,error);
  if(!created)g_runtime_preparing.store(false,std::memory_order_release);
  return created;
#else
  return ensure_runtime_started(error);
#endif
}

bool runtime_preparation_pending() {
  if (g_runtime_preparing.load(std::memory_order_acquire) ||
      g_runtime_start_in_progress.load(std::memory_order_acquire)) return true;
  auto *device = runtime_device_context();
  return device && device->runtime.task_active("python_prepare");
}

} // namespace lunaredu::micropython
