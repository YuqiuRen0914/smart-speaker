#pragma once
#include "display/pipeline/frame_damage.h"
#include "display/pipeline/damage_set.h"

namespace lunaredu::display {
struct FrameRefresh { Damage region; bool skip=false; };
ScreenDamageSet plan_frame_refresh_batch(const uint16_t *,const uint16_t *,int width,int height,int stride,
    bool baseline_valid,const FrameDamageMap *map=nullptr,unsigned window_pixels=32);
// Compare final composed pixels only against a confirmed panel baseline.
// First frames, invalid baselines and recovery always use a complete frame.
// One bounded rectangle avoids turning scattered changes into many SPI jobs.
inline FrameRefresh plan_frame_refresh(const uint16_t *before,const uint16_t *after,
                                      int width,int height,int stride,bool baseline_valid,const FrameDamageMap *map=nullptr) {
  const Damage full{0,0,width,height};
  if(!baseline_valid||!before||!after)return {full,false};
  Damage damage;
  if(map&&map->valid(width,height)&&!map->refinement_required()){
    damage=map->declared_bounds();
    // A complete declaration already selecting full output cannot be made
    // smaller by refining system pixels. Avoid reading that baseline again.
    if(int64_t(damage.width)*damage.height*4>=int64_t(width)*height*3)return {full,false};
    const auto candidates=map->refined_candidates();
    const auto extra=changed_region(before,after,width,height,stride,&candidates);
    if(damage.empty())damage=extra;
    else if(!extra.empty()){
      const int right=std::max(damage.x+damage.width,extra.x+extra.width),bottom=std::max(damage.y+damage.height,extra.y+extra.height);
      damage.x=std::min(damage.x,extra.x);damage.y=std::min(damage.y,extra.y);damage.width=right-damage.x;damage.height=bottom-damage.y;
    }
  }else damage=changed_region(before,after,width,height,stride,map);
  if(damage.empty())return {{},true};
  if(int64_t(damage.width)*damage.height*4>=int64_t(width)*height*3)return {full,false};
  return {damage,false};
}
} // namespace lunaredu::display
