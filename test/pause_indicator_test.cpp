#include "smart_speaker_pause_indicator.h"
#include <cassert>
#include <cmath>
#include <cstdio>

int main() {
  using smart_speaker::PauseIndicator;
  using smart_speaker::show_pause_for_volume;
  assert(show_pause_for_volume(0, true, false, true)); // Mute before decoder acknowledgement.
  assert(show_pause_for_volume(0, false, false, false));
  assert(show_pause_for_volume(5, false, true, true)); // Manual pause stays a pause symbol.
  assert(show_pause_for_volume(5, false, false, true));
  assert(!show_pause_for_volume(5, true, false, true));
  assert(!show_pause_for_volume(5, false, false, false));
  using smart_speaker::indicator_strokes;
  const auto bar = indicator_strokes(0, .5f);
  assert(bar[0].x1 == -54 && bar[0].x2 == 0 && bar[1].x1 == 0 && bar[1].x2 == 54);
  assert(bar[0].fill == 1 && bar[1].fill == 0);
  const auto pause = indicator_strokes(1, 0);
  assert(std::abs(pause[0].x1+5)<.001f && std::abs(pause[1].x2-5)<.001f);
  assert(std::abs(pause[0].y2-pause[0].y1-12)<.001f);
  for (int frame=0; frame<=1000; ++frame) {
    const auto strokes = indicator_strokes(frame*.001f, .37f);
    for (const auto &s : strokes) {
      assert(std::abs(s.x1)+2<=58 && std::abs(s.x2)+2<=58);
      assert(std::abs(s.y1)+2<12 && std::abs(s.y2)+2<12);
      assert(s.fill>=0 && s.fill<=1);
    }
    assert(std::abs(strokes[0].x1+strokes[1].x2)<.001f);
    assert(std::abs(strokes[0].y1+strokes[1].y1)<.001f);
  }
  // Repeat over clock wrap. Reversal preserves velocity rather than instantly
  // changing its sign; repeated rapid controls remain bounded and settle.
  for (uint32_t start : {0U, 0xffffff80U}) {
    PauseIndicator indicator;
    assert(indicator.update(start, false) == 0);
    assert(indicator.resume_opacity(start) == 0);
    assert(indicator.volume_level(35,70)==.5f);
    for(uint32_t t=10;t<120;t+=10) indicator.update(start+t,true);
    const float a=indicator.update(start+119,true);
    const float b=indicator.update(start+120,true);
    const float c=indicator.update(start+121,false);
    assert(c>b && std::abs((c-b)-(b-a))<.001f);
    assert(indicator.resume_opacity(start+121)==1);
    assert(indicator.volume_level(0,70)==.5f); // No empty-then-reappear flash.
    for(uint32_t t=130;t<=800;t+=10) indicator.update(start+t,false);
    assert(indicator.update(start+810,false)==0);
    for(uint32_t t=820;t<=2000;t+=10) {
      const float amount=indicator.update(start+t,(t/50)%2);
      assert(amount>=0 && amount<=1);
    }
    for(uint32_t t=2010;t<=2800;t+=10) indicator.update(start+t,true);
    assert(indicator.update(start+2810,true)==1);
    assert(indicator.update(start+2810,false)==1);
    for(uint32_t t=2820;t<=3600;t+=10) indicator.update(start+t,false);
    assert(indicator.update(start+3610,false)==0);
    assert(indicator.resume_opacity(start+4060)==1);
    assert(std::abs(indicator.resume_opacity(start+4185)-.5f)<.001f);
    assert(indicator.resume_opacity(start+4310)==0);
    for(uint32_t t=3620;t<=4000;t+=10) {
      indicator.update(start+t,false);
      indicator.volume_level(70,70);
    }
    assert(indicator.volume_level(70,70)==1);
  }
  std::puts("Pause indicator: continuous geometry, clipping, velocity-preserving reversal, fill, fade and wrap passed");
}
