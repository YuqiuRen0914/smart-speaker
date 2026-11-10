#pragma once
#include "lunaros_ui_interaction_layout.generated.h"
#include <algorithm>
#include <cmath>

namespace lunaredu::desktop {
struct HoneycombProjection {float x,y,diameter,detail;};
// Icon artwork and badges fade to the colored circle at the viewport edge.
// Keep this independent of size/position so both honeycomb layouts share it.
inline float honeycomb_detail(float radius){
  namespace l=honeycomb_layout;
  const float t=std::clamp((radius-l::coreRadius)/l::detailFalloffSpan,0.f,1.f);
  return 1-t*t*(3-2*t);
}
// A central cluster retains full size. Integrating the smooth scale curve
// compresses outer spacing without moving or reordering catalog coordinates.
inline HoneycombProjection project_honeycomb(float x,float y,bool labels){
  namespace l=honeycomb_layout;
  const float radius=std::hypot(x,y);
  const float t=std::clamp((radius-l::coreRadius)/l::falloffSpan,0.f,1.f);
  const float reduction=1-l::minimumScale;
  const float scale=1-reduction*t*t*(3-2*t);
  const float compression=reduction*l::spacingCompression;
  const float projected=radius<=l::coreRadius?radius:l::coreRadius+
      l::falloffSpan*(t-compression*(t*t*t-.5f*t*t*t*t))+
      std::max(0.f,radius-l::coreRadius-l::falloffSpan)*(1-compression);
  const float ratio=radius>0?projected/radius:1;
  return {x*ratio,y*ratio,(labels?l::sizeWithLabels:l::sizeWithoutLabels)*scale,
      honeycomb_detail(radius)};
}
}
