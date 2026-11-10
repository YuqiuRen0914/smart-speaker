#pragma once
#include <cstdint>
namespace lunaredu::lunaros {
// Millisecond clock, fractional Hz and absolute slots. No allocations, timer or
// RTOS dependency. Wrap is supported for intervals shorter than half the clock.
// Slow reads skip missed slots; changing rate starts a new bounded schedule.
class RateSchedule {
 public:
  void reset(){sampled_=false;}
  uint32_t wait_ms(uint32_t now,uint32_t hz) const {
    if(!sampled_)return 0;
    hz=bounded(hz);
    const auto elapsed=uint32_t(now-started_);
    if(elapsed>=0x80000000u)return 0;
    if(hz!=hz_){const auto minimum=(1000u+hz-1)/hz;return elapsed>=minimum?0:minimum-elapsed;}
    const auto since_epoch=uint32_t(now-epoch_);
    return since_epoch>=next_offset_?0:next_offset_-since_epoch;
  }
  void complete(uint32_t started,uint32_t finished,uint32_t hz){
    hz=bounded(hz);
    if(!sampled_||hz!=hz_||uint32_t(started-epoch_)>=0x80000000u){epoch_=started;hz_=hz;}
    started_=started;sampled_=true;
    // Select the first absolute slot strictly after completion. I/O overruns
    // never create a catch-up burst, including IRQ storms and clock wrap.
    const uint64_t elapsed=uint32_t(finished-epoch_);
    const uint64_t slot=elapsed*hz_/1000+1;
    next_offset_=uint32_t((slot*1000+hz_-1)/hz_);
  }
 private:
  static uint32_t bounded(uint32_t hz){return hz? (hz>1000?1000:hz):1;}
  uint32_t epoch_=0,started_=0,next_offset_=0,hz_=1;bool sampled_=false;
};
}
