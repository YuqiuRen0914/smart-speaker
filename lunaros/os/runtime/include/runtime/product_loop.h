#pragma once
#include "runtime/device.h"
#include "runtime/product_runtime.h"
#include "kernel/periodic.h"
#include "diagnostics/cpu_monitor.h"
#include "diagnostics/product_loop_timing.h"
#include <esp_timer.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace lunaredu::lunaros {
struct ProductLoopOptions {
  uint32_t period_ms;
  unsigned priority;
  bool started,foreground_io,radio;
  const char *log_tag;
  void (*startup_observer)(void *,const char *,const char *,const char *) = nullptr;
  void *startup_observer_context = nullptr;
};
// Runs on the existing caller. Product callbacks supply readiness and work;
// Runtime owns cadence, transport delivery and failed-boot maintenance.
template<class Healthy,class Tick>
[[noreturn]] void run_product_loop(Device &device,ProductLoopOptions options,Healthy healthy,Tick tick) {
  bool first_cycle=true;
  const auto observe=[&](const char *stage,const char *state,const char *error="") {
    if(first_cycle&&options.startup_observer)
      options.startup_observer(options.startup_observer_context,stage,state,error);
  };
  observe("loop_setup","enter");
  const TickType_t period=pdMS_TO_TICKS(options.period_ms);
  configASSERT(period>0);
  vTaskPrioritySet(nullptr,options.priority);
  ESP_LOGI(options.log_tag,"control loop core=%d priority=%u CPU=%dMHz",xPortGetCoreID(),
      static_cast<unsigned>(uxTaskPriorityGet(nullptr)),CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
  if(options.started&&options.foreground_io)device.use_foreground_io_checkpoints();
  TickType_t wake=xTaskGetTickCount();
  std::string error;
  std::array<uint64_t,4> timings{};
  uint32_t samples=0,over_budget=0,maximum_us=0;
  uint64_t window_us=0;
  bool timing_active=false;
  observe("loop_setup","ready");
  for(;;){
    const bool measuring=system::main_work_profiling_active();
    const uint64_t begin=measuring?esp_timer_get_time():0;
    observe("loop_health","enter");
    const bool ready=options.started&&healthy();
    const uint64_t health_done=measuring?esp_timer_get_time():0;
    observe("loop_health","ready",ready?"":"product_not_healthy");
    observe("device_poll","enter");
    device.poll(ready,DevicePollScope::SystemOnly);
    const uint64_t poll_done=measuring?esp_timer_get_time():0;
    observe("device_poll","ready");
    if(!options.started){first_cycle=false;vTaskDelayUntil(&wake,period);continue;}
    observe("radio_poll","enter");
    if(options.radio)poll_product_radio(device.context());
    const uint64_t radio_done=measuring?esp_timer_get_time():0;
    observe("radio_poll","ready");
    observe("product_tick","enter");
    device.poll_modules();
    const int hint=tick(error);
    if(measuring){
      const uint64_t done=esp_timer_get_time();
      timings[0]+=health_done-begin;timings[1]+=poll_done-health_done;
      timings[2]+=radio_done-poll_done;timings[3]+=done-radio_done;
      ++samples;const uint32_t total=uint32_t(done-begin);
      maximum_us=std::max(maximum_us,total);if(total>options.period_ms*1000)++over_budget;
      if(!window_us)window_us=begin;
      if(done-window_us>=1'000'000){
        system::ProductLoopTiming value{true,samples,options.period_ms*1000,over_budget,maximum_us};
        for(unsigned i=0;i<4;++i)value.average_us[i]=uint32_t(timings[i]/samples);
        system::publish_product_loop_timing(value);timings={};samples=over_budget=maximum_us=0;window_us=done;
      }
    }else if(timing_active){
      timings={};samples=over_budget=maximum_us=0;window_us=0;
      system::publish_product_loop_timing({});
    }
    // Clear on the measuring -> idle transition, not at every control tick.
    timing_active=measuring;
    observe("product_tick",hint<0?"failed":"ready",error.c_str());
    first_cycle=false;
    if(hint<0){vTaskDelay(period);wake=xTaskGetTickCount();continue;}
    wake=periodic_delay_base(xTaskGetTickCount(),wake,period,static_cast<unsigned>(hint));
    vTaskDelayUntil(&wake,period);
  }
}
}
