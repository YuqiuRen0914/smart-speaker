#include "graphics/animation.h"
#include <lvgl/core/lv_anim.h>
#include <algorithm>
#include <climits>
namespace lunaredu::lunaros::graphics {
float animation_progress(uint32_t elapsed,uint32_t duration,AnimationCurve curve){
  if(!duration||elapsed>=duration)return 1.f;
  // Public path callbacks consume only these fields. No lv_init, timer,
  // allocation, retained callback or global clock is needed for evaluation.
  lv_anim_t path{};
  path.duration=std::min(duration,uint32_t(INT32_MAX));
  path.act_time=int32_t(uint64_t(elapsed)*path.duration/duration);
  path.start_value=0;path.end_value=32768;
  const auto value=curve==AnimationCurve::Linear?lv_anim_path_linear(&path):
    curve==AnimationCurve::EaseInOut?lv_anim_path_ease_in_out(&path):lv_anim_path_ease_out(&path);
  return std::clamp(value/32768.f,0.f,1.f);
}
}
