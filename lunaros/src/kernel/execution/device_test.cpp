#include "lunaredu_lunaros/kernel/device_test.h"

#include <array>
#include "kernel/tasks/task_registry_internal.h"
#include <mutex>
#include <string>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "lunaredu_lunaros/kernel/managed_task.h"

namespace lunaredu::lunaros {
namespace {
constexpr const char *kTag = "device_test";
struct Entry {
  const DeviceTestDefinition *test = nullptr;
  uint32_t started_ms = 0;
  uint32_t last_ms = 0;
  bool polling = false;
};
std::array<Entry, 8> entries;
std::mutex api_mutex;
std::mutex callback_mutex;
bool worker_created = false;  // protected by api_mutex

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

bool has_polling_entries() {
  for (const auto &entry : entries) if (entry.polling) return true;
  return false;
}

void worker(void *) {
  for (;;) {
    {
      std::lock_guard<std::mutex> lock(callback_mutex);
      if (!has_polling_entries()) return;
      for (auto &entry : entries) {
        if (!entry.polling) continue;
        const uint32_t now = now_ms();
        if (now - entry.last_ms < entry.test->interval_ms) continue;
        entry.last_ms = now;
        entry.test->tick(now - entry.started_ms);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10) > 0 ? pdMS_TO_TICKS(10) : 1);
  }
}

// The managed-task trampoline releases admission after worker() returns.
// Waiting for that release also makes an immediate restart deterministic.
bool worker_admitted() { return managed_task_registry_internal().contains(kTag); }
void join_idle_worker() {
  if (!worker_created) return;
  while (worker_admitted()) vTaskDelay(1);
  worker_created = false;
}
}  // namespace

DeviceTestResult start_device_test(const DeviceTestDefinition &test, const void *config) {
  if (!test.name || !test.initialize || !test.tick || !test.shutdown ||
      !config || test.interval_ms == 0 || test.interval_ms > 60000) {
    return DeviceTestResult::InvalidArgument;
  }
  std::lock_guard<std::mutex> api_lock(api_mutex);
  std::lock_guard<std::mutex> callback_lock(callback_mutex);
  Entry *slot = nullptr;
  for (auto &entry : entries) {
    if (entry.test == &test) return DeviceTestResult::InvalidState;
    if (!entry.test && !slot) slot = &entry;
  }
  if (!slot) return DeviceTestResult::NoMemory;
  std::string registry_error;
  if (!managed_task_registry_internal().ensure_configured(registry_error)) {
    ESP_LOGE(kTag, "%s task registry unavailable: %s", test.name, registry_error.c_str());
    return DeviceTestResult::NoMemory;
  }
  const DeviceTestResult initialized = test.initialize(config);
  if (initialized != DeviceTestResult::Ok) {
    const DeviceTestResult cleaned = test.shutdown();
    if (cleaned != DeviceTestResult::Ok) *slot = {&test, 0, 0, false};
    ESP_LOGE(kTag, "%s init failed: %u; cleanup: %u", test.name,
             static_cast<unsigned>(initialized), static_cast<unsigned>(cleaned));
    return initialized;
  }
  const auto now = now_ms();
  *slot = {&test, now, now - test.interval_ms, true};
  if (!worker_created) {
    ManagedTaskDefinition definition;
    definition.id = "device_test";
    definition.owner_service_id = "device_test";
    definition.role = "peripheral demos";
    definition.stack_bytes = 8192;
    definition.priority = 2;
    definition.lifetime = lunaredu::lunaros::ManagedTaskLifetime::Feature;
    std::string error;
    if (!create_managed_task(definition, worker, nullptr, nullptr, error)) {
      slot->polling = false;
      if (test.shutdown() == DeviceTestResult::Ok) *slot = {};
      ESP_LOGE(kTag, "%s worker admission failed: %s", test.name, error.c_str());
      return DeviceTestResult::NoMemory;
    }
    worker_created = true;
  }
  ESP_LOGI(kTag, "%s started (observe hardware; this is not an automatic PASS)", test.name);
  return DeviceTestResult::Ok;
}

DeviceTestResult stop_device_test(const DeviceTestDefinition &test) {
  std::lock_guard<std::mutex> api_lock(api_mutex);
  DeviceTestResult result = DeviceTestResult::Ok;
  bool idle = false;
  {
    std::lock_guard<std::mutex> callback_lock(callback_mutex);
    for (auto &entry : entries) {
      if (entry.test != &test) continue;
      entry.polling = false;
      result = test.shutdown();
      if (result == DeviceTestResult::Ok) entry = {};
      break;
    }
    idle = !has_polling_entries();
  }
  if (idle) join_idle_worker();
  if (result != DeviceTestResult::Ok) ESP_LOGE(kTag, "%s cleanup failed: %u", test.name, static_cast<unsigned>(result));
  return result;
}

}  // namespace lunaredu::lunaros
