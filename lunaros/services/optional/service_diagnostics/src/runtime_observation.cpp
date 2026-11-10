#include "lunaros_diagnostics_runtime.h"
#include "kernel/thread_observation.h"
#include "kernel/memory_status.h"
#include <memory>
#include <new>
#include <sdkconfig.h>
#include <esp_system.h>
#include <esp_timer.h>
namespace lunaredu::lunaros {
namespace {
const char *reset_reason_name(esp_reset_reason_t reason) {
  switch (reason) {
  case ESP_RST_POWERON: return "power_on";
  case ESP_RST_EXT: return "external";
  case ESP_RST_SW: return "software";
  case ESP_RST_PANIC: return "panic";
  case ESP_RST_INT_WDT: return "interrupt_watchdog";
  case ESP_RST_TASK_WDT: return "task_watchdog";
  case ESP_RST_WDT: return "watchdog";
  case ESP_RST_DEEPSLEEP: return "deep_sleep";
  case ESP_RST_BROWNOUT: return "brownout";
  case ESP_RST_SDIO: return "sdio";
  case ESP_RST_USB: return "usb";
  case ESP_RST_JTAG: return "jtag";
  case ESP_RST_EFUSE: return "efuse";
  case ESP_RST_PWR_GLITCH: return "power_glitch";
  case ESP_RST_CPU_LOCKUP: return "cpu_lockup";
  case ESP_RST_UNKNOWN:
  default: return "unknown";
  }
}
}
void write_task_runtime_counters(JsonObject payload){
#ifdef CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ
  payload["clock_mhz"]=CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
#endif
  using Records=std::array<NativeThreadCounterSnapshot,kNativeThreadCounterReadCapacity>;
  std::unique_ptr<Records> records(new(std::nothrow) Records{});
  if(!records){payload["available"]=false;payload["error"]="thread_snapshot_allocation_failed";return;}
  const auto facts=capture_native_thread_counters(std::span<NativeThreadCounterSnapshot>(*records));
  payload["available"]=facts.available&&facts.complete&&facts.runtime_counter_bits&&facts.total_counter_available;
  payload["complete"]=facts.complete;payload["observed_tasks"]=facts.observed_threads;
  payload["captured_tasks"]=facts.captured_threads;
  payload["counter_us"]=facts.total_runtime_counter;
#if defined(CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER) && CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER
  payload["counter_unit"]="microseconds";
#else
  payload["counter_unit"]="native_ticks";
#endif
  if(!facts.error.empty())payload["error"]=facts.error;
  JsonArray entries=payload["tasks"].to<JsonArray>();
  if(!facts.runtime_counter_bits)return;
  for(size_t i=0;i<facts.captured_threads;++i){
    auto entry=entries.add<JsonObject>();entry["name"]=(*records)[i].name.data();
    entry["runtime_us"]=(*records)[i].runtime_counter;
  }
}
void write_system_runtime_diagnostics(JsonObject payload){
  const auto reset=esp_reset_reason();
  payload["uptime_ms"]=uint64_t(esp_timer_get_time())/1000;
  payload["reset_reason"]=int(reset);payload["reset_reason_name"]=reset_reason_name(reset);
  const auto dma=system::read_memory_pool_free_snapshot(system::MemoryPool::Dma);
  const auto internal=system::read_memory_pool_free_snapshot(system::MemoryPool::Internal);
  payload["dma_free_bytes"]=dma.free_bytes;payload["dma_largest_block"]=dma.largest_free_bytes;
  payload["dma_minimum_free_bytes"]=dma.minimum_free_bytes;
  payload["internal_free_bytes"]=internal.free_bytes;payload["internal_largest_block"]=internal.largest_free_bytes;
}
}
