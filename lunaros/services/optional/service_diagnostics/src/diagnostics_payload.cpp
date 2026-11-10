#include "device_api/adapters/application_projection.h"
#include "diagnostics_payload.h"
#include "device_api/bootstrap_metrics_json.h"
#include "app/codec/application_maintenance_json.h"

#include <algorithm>
#include <cstdio>

#include <esp_random.h>
#include <esp_timer.h>
#include <array>
#include "device_api/device_operation_types.h"
#include "diagnostic_inventory.h"
#include "diagnostic_capture_operations.h"

#include "kernel/runtime.h"
#include "kernel/service.h"
#include "kernel/service_executor.h"
#include "kernel/managed_task.h"
#include "device_api/device_api_context.h"
#include "device_api/device_projection.h"
#include "system/system_info.h"
#include "diagnostics/runtime_diagnostics.h"
#include "official_tools_operations.h"

namespace lunaredu::lunaros::internal {
namespace {
const char *boot_id() {
  static const auto id = [] {
    std::array<char, 17> value{};
    std::snprintf(value.data(), value.size(), "%08x%08x",
                static_cast<unsigned>(esp_random()),
                static_cast<unsigned>(esp_random()));
    return value;
  }();
  return id.data();
}
} // namespace

void fill_system_diagnostics_payload(DeviceApiContext &device, JsonObject payload) {
  const auto started = static_cast<uint64_t>(esp_timer_get_time()) / 1000;
  payload["contract_version"] = 2;
  fill_official_tools_status(payload["official_tools"].to<JsonObject>());
  fill_application_maintenance_payload(device_api_application_maintenance(device), payload["application_maintenance"].to<JsonObject>());
  fill_bootstrap_metrics_payload(device, payload["bootstrap"].to<JsonObject>());
  payload["boot_id"] = boot_id();
  payload["diagnostic_snapshot_version"] = 1;
  payload["consistency"] = "best_effort";
  payload["sample_started_ms"] = started;

  const auto info = lunaredu::system::read_system_info_snapshot();
  JsonObject system = payload["system"].to<JsonObject>();
  system["application"] = info.application_name;
  system["firmware"] = info.application_version;
  system["compile_time"] = info.application_compile_time;
  system["elf_sha256"] = info.elf_sha256;
  system["idf_version"] = info.idf_version;
  system["chip"] = info.chip_model_name;
  system["uptime_ms"] = info.uptime_ms;
  system["internal_free_bytes"] = info.internal_free_bytes;
  system["minimum_free_heap_bytes"] = info.minimum_free_heap_size;
  system["psram_free_bytes"] = info.psram_free_bytes;
  system["psram_total_bytes"] = info.psram_total_bytes;
  system["running_partition"] = info.running_ota_partition;

  const auto runtime = lunaredu::system::read_runtime_diagnostics_snapshot();
  JsonObject scheduler = payload["scheduler"].to<JsonObject>();
  scheduler["tick_hz"] = runtime.tick_hz;
  scheduler["tick_count"] = runtime.tick_count;
  scheduler["configured_cpu_mhz"] = runtime.configured_cpu_mhz;
  scheduler["freertos_task_count"] = runtime.freertos_task_count;
  scheduler["power_management_enabled"] = runtime.power_management_enabled;
  scheduler["tickless_idle_enabled"] = runtime.tickless_idle_enabled;
  scheduler["task_runtime_stats_enabled"] = runtime.task_runtime_stats_enabled;
  scheduler["cpu_monitor_active"] = runtime.cpu_monitor_active;
  scheduler["cpu_monitor_remaining_ms"] = runtime.cpu_monitor_remaining_ms;
  scheduler["cpu_usage_available"] = runtime.cpu_usage_available;
  scheduler["cpu_sample_interval_ms"] = runtime.cpu_sample_interval_ms;
  if (runtime.cpu_usage_available) {
    scheduler["core0_busy_percent"] = runtime.core0_busy_percent_tenths / 10.0;
    scheduler["core1_busy_percent"] = runtime.core1_busy_percent_tenths / 10.0;
  }
  scheduler["sampled_task_count"] = runtime.sampled_task_count;
  scheduler["cpu_capture_kind"] = runtime.cpu_capture_fallback?"full_thread_fallback":"runtime_counters";
  JsonArray cpu_tasks = scheduler["top_tasks"].to<JsonArray>();
  for (size_t index = 0; index < runtime.top_task_count; ++index) {
    const auto &task = runtime.top_tasks[index];
    JsonObject item = cpu_tasks.add<JsonObject>();
    item["name"] = task.name.data();
    item["core_id"] = task.core_id;
    item["priority"] = task.priority;
    item["core_percent"] = task.core_percent_tenths / 10.0;
    item["minimum_free_stack_bytes"] = nullptr;
  }
  JsonObject memory = payload["memory"].to<JsonObject>();
  memory["internal_largest_free_bytes"] = runtime.internal_largest_free_bytes;
  memory["psram_largest_free_bytes"] = runtime.psram_largest_free_bytes;
  memory["unit"] = "bytes";
  memory["internal_free_bytes"] = info.internal_free_bytes;
  memory["psram_free_bytes"] = info.psram_free_bytes;
  memory["psram_total_bytes"] = info.psram_total_bytes;
  memory["per_application_attribution"] = "unavailable";
  memory["heap_regions_additive"] = false;
  memory["minimum_free_heap_scope"] = "esp_get_minimum_free_heap_size";

  const auto services = device.runtime.service_snapshot();
  JsonObject service_summary = payload["service_summary"].to<JsonObject>();
  service_summary["revision"] = services.revision;
  service_summary["required_ready"] = services.required_services_ready;
  service_summary["count"] = services.service_count;
  size_t unavailable = 0;
  JsonArray service_items = payload["services"].to<JsonArray>();
  for (size_t index = 0; index < services.service_count; ++index) {
    const auto &service = services.services[index];
    if (!service.available) ++unavailable;
    JsonObject item = service_items.add<JsonObject>();
    item["id"] = service.id;
    item["state"] = service_state_name(service.state);
    item["available"] = service.available;
    item["criticality"] = service_criticality_name(service.criticality);
    if (!service.detail.empty()) item["detail"] = service.detail;
  }
  service_summary["unavailable"] = unavailable;

  const auto tasks = device.runtime.task_snapshot();
  JsonObject task_summary = payload["task_summary"].to<JsonObject>();
  task_summary["revision"] = tasks.revision;
  task_summary["active"] = tasks.active_tasks;
  task_summary["complete"] = tasks.complete;
  task_summary["captured_tasks"] = tasks.tasks.size();
  task_summary["resident"] = tasks.resident_tasks;
  task_summary["feature"] = tasks.feature_tasks;
  task_summary["transient"] = tasks.transient_tasks;
  task_summary["internal_stack_bytes"] = tasks.internal_stack_bytes;
  task_summary["external_stack_bytes"] = tasks.external_stack_bytes;
  task_summary["allocation_policy"] = "physical_memory_and_execution_admission";
  JsonArray task_items = payload["tasks"].to<JsonArray>();
  for (const auto &task : tasks.tasks) {
    if (!task.active) continue;
    JsonObject item = task_items.add<JsonObject>();
    item["id"] = task.id.data();
    item["owner_id"] = task.owner_service_id.data();
    item["owner_kind"] = managed_task_owner_kind_name(task.owner_kind);
    if (task.owner_kind == ManagedTaskOwnerKind::Service)
      item["owner_service"] = task.owner_service_id.data();
    item["role"] = task.role.data();
    item["stack_bytes"] = task.stack_bytes;
    item["priority"] = task.priority;
    item["core_affinity"] = task.core_affinity;
    item["memory"] = managed_task_memory_name(task.memory);
    item["started"] = task.native_handle != 0;
    if (task.native_handle != 0) {
      item["minimum_free_stack_bytes"] = task.minimum_free_stack_bytes;
    }
  }

  ServiceExecutorSnapshot executors[8] = {};
  const size_t executor_count = device.runtime.executor_snapshot(executors, 8);
  JsonArray executor_items = payload["executors"].to<JsonArray>();
  for (size_t index = 0; index < std::min<size_t>(executor_count, 8); ++index) {
    JsonObject item = executor_items.add<JsonObject>();
    item["id"] = executors[index].id;
    item["owner_service"] = executors[index].owner_service_id;
    item["queue_depth"] = executors[index].queue_depth;
    item["queue_depth_meaning"] = "configured_capacity";
    item["queued_work_count"] = nullptr;
    item["bound"] = executors[index].bound;
  }
  auto executor_summary = payload["executor_summary"].to<JsonObject>();
  executor_summary["total"] = executor_count;
  executor_summary["returned"] = executor_items.size();
  executor_summary["truncated"] = executor_count > executor_items.size();
  service_summary["returned"] = service_items.size();
  service_summary["truncated"] = services.service_count > service_items.size();
  task_summary["returned"] = task_items.size();
  task_summary["coverage"] = "managed_tasks_only";
  task_summary["truncated"] = tasks.active_tasks > task_items.size();
  diagnostic_capture_status(payload["capture"].to<JsonObject>());
  fill_diagnostic_resources(device, payload["resources"].to<JsonObject>());

  auto application = payload["application"].to<JsonObject>();
  application["status"] = "unsupported";
  // Reuse the optional App projection so bare Device firmware does not link App code.
  if (auto *registry = device.projections()) {
    const auto *op = registry->find_operation("app", "status.get");
    if (op && op->json_result_provider) {
      auto result = op->json_result_provider(device, application["data"].to<JsonObject>());
      application["status"] = result.ok ? "observed" : "unavailable";
      if (!result.ok) application["error_code"] = result.error;
    }
  }
  fill_diagnostic_activities(device, payload["activities"].to<JsonObject>());
  payload["sample_finished_ms"] = static_cast<uint64_t>(esp_timer_get_time()) / 1000;
}

} // namespace lunaredu::lunaros::internal
