#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace lunaredu::remote_control {
// Wrap-aware ordering and a bounded replay window shared by input and commands.
// A counter restart needs an idle interval and an explicit safe restart policy;
// duplicates never extend that interval. Binding changes reset the whole window.
class FrameSequence {
 public:
  bool allows(uint32_t sequence,uint32_t timestamp,int64_t now,int64_t restart_us,
      bool safe_restart=false) const {
    if(now<0)return false;
    if(!seen_)return true;
    if(now<received_us_)return false;
    for(size_t i=0;i<count_;++i)
      if(history_[i].sequence==sequence&&history_[i].timestamp==timestamp)return false;
    const uint32_t delta=sequence-sequence_;
    if(delta==0)return false;
    const uint32_t clock_delta=timestamp-timestamp_;
    // Relative peer time needs no clock synchronization. Reject old clock values
    // and implausible forward jumps, including packets from a retired boot.
    if(delta<0x80000000U&&clock_delta<0x80000000U&&
        uint64_t(clock_delta)<=uint64_t((now-received_us_)/1000)+2000)return true;
    if(delta<0x80000000U&&clock_delta<0x80000000U)return false;
    return safe_restart&&restart_us>0&&now-received_us_>=restart_us;
  }
  void record(uint32_t sequence,uint32_t timestamp,int64_t now) {
    sequence_=sequence;timestamp_=timestamp;received_us_=now;seen_=true;
    history_[next_]={sequence,timestamp};next_=(next_+1)%history_.size();
    if(count_<history_.size())++count_;
  }
  bool timestamp_restarted(uint32_t timestamp) const {
    return seen_&&timestamp!=timestamp_&&uint32_t(timestamp-timestamp_)>=0x80000000U;
  }
  void reset(){*this={};}
 private:
  struct Identity{uint32_t sequence=0,timestamp=0;};
  std::array<Identity,16> history_{};
  size_t count_=0,next_=0;
  uint32_t sequence_=0,timestamp_=0;
  int64_t received_us_=0;
  bool seen_=false;
};
}
