#pragma once
#include <algorithm>
#include <cstdint>

namespace lunaredu::audio {
class OutputCadence {
public:
  template<class Clock> uint64_t observe(bool enabled,uint64_t generation,Clock clock) {
    if (!enabled) { *this={};return 0; }
    const uint64_t now=clock();
    const uint64_t gap=observed_ && generation_==generation && now>=previous_?now-previous_:0;
    maximum_=std::max(maximum_,gap);
    previous_=now;generation_=generation;observed_=true;
    return gap;
  }
  uint64_t take_maximum() { const auto result=maximum_;maximum_=0;return result; }
private:
  uint64_t previous_=0,maximum_=0;
  uint64_t generation_=0;
  bool observed_=false;
};
}
