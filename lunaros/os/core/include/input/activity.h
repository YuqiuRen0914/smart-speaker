#pragma once
#include <algorithm>
#include <cstdint>
namespace lunaredu::lunaros::input {
// Held/unusable input at boot has not earned UI activity. Observe a valid
// neutral state first, as navigation rearming already requires.
class NeutralActivityGate {
 public:
  bool observe(bool valid,bool centered){
    if(!valid)armed_=false;
    else if(centered)armed_=true;
    return valid&&armed_&&!centered;
  }
 private: bool armed_=false;
};
// Owned by an input observer, independent of devices, Apps and task creation.
// A release retains the active rate briefly, avoiding mode chatter and losing
// the next gesture while filters, release actions or navigation settle.
class ActivityWindow {
 public:
  static constexpr uint32_t idle_hz=10, active_hz=60, fallback_hz=30, hold_ms=500;
  void observe(uint32_t now, bool active) {
    if(active){last_activity_=now;seen_=true;}
  }
  void reset(){seen_=false;}
  bool active(uint32_t now) const {return seen_ && uint32_t(now-last_activity_)<hold_ms;}
  uint32_t rate_hz(uint32_t now, bool reliable_wake=true, uint32_t required_hz=0) const {
    return std::max(required_hz,active(now)?active_hz:reliable_wake?idle_hz:fallback_hz);
  }
 private:
  uint32_t last_activity_=0;bool seen_=false;
};
}
