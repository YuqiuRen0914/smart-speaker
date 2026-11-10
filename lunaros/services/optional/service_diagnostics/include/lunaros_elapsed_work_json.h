#pragma once
#include <ArduinoJson.h>
#include <array>
#include <cstddef>
namespace lunaredu::lunaros {
// Diagnostic serialization stays outside Core and timing owners. Names and
// snapshots are borrowed from the recording owner; nested elapsed windows
// include waits/preemption and are not independent task CPU measurements.
template<class Snapshot,size_t Count> void write_elapsed_work_json(JsonObject payload,
    const Snapshot &sample,const std::array<const char *,Count> &names,bool task_flags=false){
  static_assert(std::tuple_size_v<decltype(sample.stages)> == Count);
  payload["available"]=sample.available;payload["basis"]="elapsed_including_preemption";
  if(!sample.available)return;
  payload["window_us"]=sample.window_us;payload["task_basis"]="not_measured_use_task_cpu_windows";
  for(size_t i=0;i<Count;++i){auto value=payload[names[i]].template to<JsonObject>();const auto &s=sample.stages[i];
    value["samples"]=s.samples;value["average_us"]=s.average_us;value["maximum_us"]=s.maximum_us;value["settled_task_us"]=s.settled_task_us;
    if(task_flags)value["task_measured"]=false;
  }
}
}
