#include "capture_control.h"
#include "espnow_diagnostics.h"
#include "official_tools.h"
#include "heap_tools.h"
#include "gprof_tools.h"
#include "diagnostic_outbox.h"
#include "insights_tools.h"
#include "coredump_tools.h"
#include "diagnostics/session.h"

#include <esp_timer.h>
#include <esp_heap_caps.h>

#include <esp_log.h>
#include "kernel/managed_task.h"
#include "kernel/runtime.h"
#include "diagnostics/runtime_diagnostics.h"
#include "system/system_info.h"

namespace lunaredu::system {

void poll_serial_diagnostics(lunaredu::lunaros::KernelRuntime &runtime) {
  lunaredu::lunaros::internal::poll_espnow_diagnostics();
  lunaredu::lunaros::internal::diagnostic_outbox::poll();
#if CONFIG_GPROF_ENABLE
  lunaredu::lunaros::internal::gprof_tools::poll(lunaredu::lunaros::diagnostic_session_status().active);
#endif
#if CONFIG_LUNAROS_DIAGNOSTIC_HEAP
  lunaredu::lunaros::internal::heap_tools::poll(lunaredu::lunaros::diagnostic_session_status().active);
#endif
#if CONFIG_LUNAROS_DIAGNOSTIC_APPTRACE || CONFIG_LUNAROS_DIAGNOSTIC_SYSTEMVIEW
  if (lunaredu::lunaros::internal::official_tools::needs_poll())
    lunaredu::lunaros::internal::official_tools::poll(
        lunaredu::lunaros::diagnostic_session_status().active);
#endif
#if CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS
  if (!cpu_monitor_active()) return;
  static uint32_t last_report_ms = 0;
  const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
  if (now_ms - last_report_ms < 10'000) return;
  last_report_ms = now_ms;

  const auto cpu = read_runtime_diagnostics_snapshot();
  const auto tasks = runtime.task_summary();
  const auto info = read_system_info_snapshot();
  const auto internal_minimum = heap_caps_get_minimum_free_size(
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  ESP_LOGI("system_probe",
       "CPU interval_ms=%lu core0=%.1f%% core1=%.1f%% tasks=%lu/%lu tick_hz=%lu",
       static_cast<unsigned long>(cpu.cpu_sample_interval_ms),
       cpu.core0_busy_percent_tenths / 10.0,
       cpu.core1_busy_percent_tenths / 10.0,
       static_cast<unsigned long>(cpu.sampled_task_count),
       static_cast<unsigned long>(cpu.freertos_task_count),
       static_cast<unsigned long>(cpu.tick_hz));
  ESP_LOGI("system_probe",
       "MEM internal_free=%lu internal_min=%lu internal_largest=%llu "
       "psram_free=%lu psram_largest=%llu managed=%lu "
       "internal_stack=%lu external_stack=%lu",
       static_cast<unsigned long>(info.internal_free_bytes),
       static_cast<unsigned long>(internal_minimum),
       static_cast<unsigned long long>(cpu.internal_largest_free_bytes),
       static_cast<unsigned long>(info.psram_free_bytes),
       static_cast<unsigned long long>(cpu.psram_largest_free_bytes),
       static_cast<unsigned long>(tasks.active_tasks),
       static_cast<unsigned long>(tasks.internal_stack_bytes),
       static_cast<unsigned long>(tasks.external_stack_bytes));
  if (!cpu.cpu_usage_available) return;
  for (size_t index = 0; index < cpu.top_task_count && index < 8; ++index) {
    const auto &task = cpu.top_tasks[index];
    ESP_LOGI("system_probe",
         "TASK name=%s core=%ld priority=%lu cpu=%.1f%%",
         task.name.data(), static_cast<long>(task.core_id),
         static_cast<unsigned long>(task.priority),
         task.core_percent_tenths / 10.0);
  }
#else
  (void)runtime;
#endif
}

} // namespace lunaredu::system
