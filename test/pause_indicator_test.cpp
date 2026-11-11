#include "smart_speaker_pause_indicator.h"
#include <cassert>
#include <cmath>
#include <cstdio>
int main() {
  using smart_speaker::VolumeIndicator;
  for (uint32_t start : {0U, 0xffffff80U}) {
    VolumeIndicator indicator;
    assert(indicator.update(start,35,70)==.5f);
    for(uint32_t t=20;t<=400;t+=20) indicator.update(start+t,0,70);
    assert(indicator.update(start+420,0,70)==0); // Mute empties the bar.
    for(uint32_t t=440;t<=840;t+=20) indicator.update(start+t,70,70);
    assert(indicator.update(start+860,70,70)==1);
    const auto before=indicator.update(start+880,0,70);
    const auto after=indicator.update(start+881,70,70);
    assert(after>=before && after-before<.03f);
  }
  assert(smart_speaker::display_frame_wait_ms(0,20)==20);
  assert(smart_speaker::display_frame_wait_ms(4000,20)==16);
  assert(smart_speaker::display_frame_wait_ms(25000,20)==0);
  std::puts("Volume smoothing: mute, reversal, wrap and frame budget passed");
}
