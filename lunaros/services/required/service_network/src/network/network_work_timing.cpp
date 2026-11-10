#include "lunaros_network_work_timing.h"
#ifdef ESP_PLATFORM
#include "diagnostics/cpu_monitor.h"
#include <esp_timer.h>
#include <esp_attr.h>
#endif
namespace lunaredu::lunaros {
#if defined(ESP_PLATFORM) && !CONFIG_LUNAREDU_LUNAROS_CPU_MONITOR
NetworkWorkTiming::Snapshot read_network_work_timing(){return {};}
NetworkWorkScope::NetworkWorkScope(NetworkWorkStage stage):stage_(stage){}
NetworkWorkScope::~NetworkWorkScope()=default;
#else
namespace {
#if defined(ESP_PLATFORM) && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
EXT_RAM_BSS_ATTR
#endif
NetworkWorkTiming timing;
bool collecting=false;
void collect(bool value){if(collecting!=value){timing.enable(value);collecting=value;}}
bool enabled(){
#ifdef ESP_PLATFORM
  return system::main_work_profiling_active();
#else
  return false;
#endif
}
}
NetworkWorkTiming::Snapshot read_network_work_timing(){collect(enabled());return timing.snapshot();}
NetworkWorkScope::NetworkWorkScope(NetworkWorkStage stage):stage_(stage),active_(enabled()){
  collect(active_);
#ifdef ESP_PLATFORM
  if(active_)started_=esp_timer_get_time();
#endif
}
NetworkWorkScope::~NetworkWorkScope(){
#ifdef ESP_PLATFORM
  if(active_){const auto end=uint64_t(esp_timer_get_time());timing.record(size_t(stage_),end-started_,end);}
#endif
}
#endif
}
