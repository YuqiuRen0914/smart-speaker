#pragma once
#include <cstdint>
namespace lunaredu::lunaros {
// Caller-serialized demand handshake. Registry retirement, not a cleared SDK
// handle, permits replacement. Producers pin the publish interval; workers
// retire only BETWEEN borrowed work items and after owner-declared idle grace.
class WorkerDemand {
 public:
  enum class Phase { Idle, Starting, Running, Retiring };
  void request(bool wanted,uint32_t now){
    if(wanted||wanted_!=wanted)idle_at_=now;
    wanted_=wanted;
  }
  bool wanted()const{return wanted_||publishers_!=0;}
  void pin_publish(){++publishers_;}
  void unpin_publish(){if(publishers_)--publishers_;}
  bool admit(){if(!wanted()||phase_!=Phase::Idle)return false;phase_=Phase::Starting;return true;}
  void started(){if(phase_==Phase::Starting)phase_=Phase::Running;}
  void failed(){if(phase_==Phase::Starting)phase_=Phase::Idle;}
  bool retirement_due(uint32_t now,uint32_t grace)const{
    return phase_==Phase::Running&&!wanted()&&uint32_t(now-idle_at_)>=grace;
  }
  bool retire(uint32_t now,uint32_t grace,bool work_held=false){
    if(work_held||!retirement_due(now,grace))return false;
    phase_=Phase::Retiring;return true;
  }
  void registry_observed(bool present){if(!present&&phase_!=Phase::Starting)phase_=Phase::Idle;}
  bool satisfied(bool running)const{return !wanted()||(running&&phase_!=Phase::Retiring);}
  Phase phase()const{return phase_;}
 private:
  Phase phase_=Phase::Idle;
  bool wanted_=false;
  uint32_t idle_at_=0,publishers_=0;
};
}
