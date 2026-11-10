#pragma once
#include <algorithm>
#include <cstdint>
namespace lunaredu::audio {
class PcmClock {
 public:
  void reset() { microseconds_=remainder_=0; rate_=0; }
  void advance(uint32_t frames,uint32_t rate) {
    if (!rate) return;
    if (rate!=rate_) { remainder_=0; rate_=rate; }
    const uint64_t n=uint64_t(frames)*1000000+remainder_;
    microseconds_+=n/rate; remainder_=n%rate;
  }
  uint32_t milliseconds() const { return uint32_t(std::min<uint64_t>(microseconds_/1000,UINT32_MAX)); }
 private:
  uint64_t microseconds_=0, remainder_=0;
  uint32_t rate_=0;
};
}
