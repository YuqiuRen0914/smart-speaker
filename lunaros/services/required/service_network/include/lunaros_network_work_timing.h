#pragma once
#include "diagnostics/elapsed_work_timing.h"
#include <utility>
namespace lunaredu::lunaros {
// Network maintenance-owner observations, not task CPU. Nested stages include
// preemption and waiting; caller must not sum them with enclosing Main work.
enum class NetworkWorkStage:size_t {Tls,WifiMonitor,Wifi,Configuration,Bluetooth,EspNow,Transitions,Access,Observation,Count};
inline constexpr std::array<const char *,size_t(NetworkWorkStage::Count)> network_work_stage_names={"tls","wifi_monitor","wifi","configuration","bluetooth","espnow","transitions","access","observation"};
using NetworkWorkTiming=system::ElapsedWorkTiming<size_t(NetworkWorkStage::Count)>;
NetworkWorkTiming::Snapshot read_network_work_timing();
class NetworkWorkScope {
 public:
  explicit NetworkWorkScope(NetworkWorkStage);
  ~NetworkWorkScope();
  NetworkWorkScope(const NetworkWorkScope &)=delete;
 private:
  NetworkWorkStage stage_;bool active_=false;uint64_t started_=0;
};
template<class Action> decltype(auto) network_work(NetworkWorkStage stage,Action &&action){
  NetworkWorkScope scope(stage);return std::forward<Action>(action)();
}
}
