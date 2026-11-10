#pragma once

#if defined(ESP_PLATFORM) && CONFIG_IDF_TARGET_ESP32S3
#include <array>
#include <cstdint>
#include <esp_cpu.h>
#include <esp_ipc.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
extern "C" {
#include <eri.h>
}
#include <xtensa-debug-module.h>
#include <xtensa/xt_perf_consts.h>

namespace lunaredu::system::cpu_detail {
// Diagnostic owner only, serialized by the CPU monitor. IPC callbacks perform
// register accesses, never allocations, logging, waits or application callbacks.
// Counts cover the whole core (including IRQs), not the calling task. Windows
// for different events are sequential and must not be added as simultaneous.
class PerfCounterProbe {
 public:
  struct Window {
    uint64_t elapsed_us=0;
    uint32_t cycles=0,events=0,selector=0,mask=0;
    bool available=false,refused=false;
  };
  const auto &sample(bool enabled) {
    for(unsigned core=0;core<banks_.size();++core) {
      auto &bank=banks_[core];bank.latest={};bank.collect=enabled;
      if(!enabled && !bank.owned){bank.refused=false;continue;}
      if(enabled && bank.refused){bank.latest.refused=true;continue;}
      if(core==esp_cpu_get_core_id())step(&bank);
      else if(esp_ipc_call_blocking(core,step,&bank)!=ESP_OK)
        bank.latest.refused=true;
    }
    for(unsigned core=0;core<banks_.size();++core)windows_[core]=banks_[core].latest;
    return windows_;
  }
  void stop(){(void)sample(false);}
 private:
  struct Bank {
    bool owned=false,collect=false,refused=false;
    unsigned phase=0;
    uint64_t began_us=0;
    Window latest;
  };
  struct Event {uint32_t selector,mask;};
  static constexpr std::array<Event,9> events_{{
    {XTPERF_CNT_D_STALL,XTPERF_MASK_D_STALL_CACHE_MISS},
    {XTPERF_CNT_I_STALL,XTPERF_MASK_I_STALL_CACHE_MISS},
    {XTPERF_CNT_D_STALL,XTPERF_MASK_D_STALL_ALL},
    {XTPERF_CNT_I_STALL,XTPERF_MASK_I_STALL_ALL},
    {XTPERF_CNT_I_STALL,XTPERF_MASK_I_STALL_BUSY},
    {XTPERF_CNT_I_STALL,XTPERF_MASK_I_STALL_IN_PIF},
    {XTPERF_CNT_D_STALL,XTPERF_MASK_D_STALL_BUSY},
    {XTPERF_CNT_D_STALL,XTPERF_MASK_D_STALL_IN_PIF},
    {XTPERF_CNT_D_STALL,XTPERF_MASK_D_STALL_STORE_BUF_FULL}
  }};
  static uint32_t control(Event event) {
    return (uint32_t(PMCTRL_TRACELEVEL_MASK)<<PMCTRL_TRACELEVEL_SHIFT) |
        (event.selector<<PMCTRL_SELECT_SHIFT) | (event.mask<<PMCTRL_MASK_SHIFT);
  }
  static uint32_t cycles_control(){return control({XTPERF_CNT_CYCLES,XTPERF_MASK_CYCLES});}
  static bool still_owned(const Bank &bank) {
    return eri_read(ERI_PERFMON_PGM)==PGM_PMEN &&
        eri_read(ERI_PERFMON_PMCTRL0)==cycles_control() &&
        eri_read(ERI_PERFMON_PMCTRL0+4)==control(events_[bank.phase]);
  }
  static void release(Bank &bank) {
    if(still_owned(bank)) {
      eri_write(ERI_PERFMON_PGM,0);
      eri_write(ERI_PERFMON_PMCTRL0,0);eri_write(ERI_PERFMON_PMCTRL0+4,0);
      eri_write(ERI_PERFMON_PM0,0);eri_write(ERI_PERFMON_PM0+4,0);
    }
    bank.owned=false;
  }
  static void begin(Bank &bank) {
    eri_write(ERI_PERFMON_PGM,0);
    eri_write(ERI_PERFMON_PM0,0);eri_write(ERI_PERFMON_PM0+4,0);
    eri_write(ERI_PERFMON_PMCTRL0,cycles_control());
    eri_write(ERI_PERFMON_PMCTRL0+4,control(events_[bank.phase]));
    bank.began_us=uint64_t(esp_timer_get_time());
    eri_write(ERI_PERFMON_PGM,PGM_PMEN);
  }
  static void step(void *opaque) {
    auto &bank=*static_cast<Bank *>(opaque);
    if(!bank.collect){release(bank);bank.refused=false;return;}
    if(!bank.owned) {
      // Do not steal a debugger, apptrace or another profiler's counter bank,
      // even when its global counting enable is currently clear.
      if(eri_read(ERI_PERFMON_PGM) || eri_read(ERI_PERFMON_PMCTRL0) ||
         eri_read(ERI_PERFMON_PMCTRL0+4) || eri_read(ERI_PERFMON_PM0) ||
         eri_read(ERI_PERFMON_PM0+4) || eri_read(ERI_PERFMON_PMSTAT0) ||
         eri_read(ERI_PERFMON_PMSTAT0+4)) {
        bank.refused=bank.latest.refused=true;return;
      }
      bank.phase=0;bank.owned=true;begin(bank);return;
    }
    if(!still_owned(bank)) {
      bank.owned=false;bank.refused=bank.latest.refused=true;return;
    }
    eri_write(ERI_PERFMON_PGM,0);
    const uint64_t elapsed=uint64_t(esp_timer_get_time())-bank.began_us;
    const auto cycles=eri_read(ERI_PERFMON_PM0),events=eri_read(ERI_PERFMON_PM0+4);
    const auto overflow=eri_read(ERI_PERFMON_PMSTAT0)|eri_read(ERI_PERFMON_PMSTAT0+4);
    // Restore enable before using the same ownership-fenced retirement path.
    eri_write(ERI_PERFMON_PGM,PGM_PMEN);
    if(!elapsed || !cycles || elapsed>8'000'000 || (overflow&PMSTAT_OVFL)) {
      release(bank);bank.refused=bank.latest.refused=true;return;
    }
    const auto event=events_[bank.phase];
    bank.latest={elapsed,cycles,events,event.selector,event.mask,true,false};
    bank.phase=(bank.phase+1)%events_.size();begin(bank);
  }
  std::array<Bank,CONFIG_FREERTOS_NUMBER_OF_CORES> banks_{};
  std::array<Window,CONFIG_FREERTOS_NUMBER_OF_CORES> windows_{};
};
} // namespace lunaredu::system::cpu_detail
#endif
