#pragma once
#include "desktop/shell/shell.h"
#include "display/pipeline/frame_damage.h"
#include <array>
#include <cmath>
#include <utility>

namespace lunaredu::desktop {
// Conservative bounds of pixels the system actually paints. Kept with the
// last accepted frame so hiding or moving chrome also erases its old bounds.
struct ChromeCoverage {
  std::array<display::Damage,3> areas{};
  unsigned count=0;
  void mark(display::FrameDamageMap &map) const {
    for(unsigned i=0;i<count;++i)map.mark_refined(areas[i]);
  }
  static ChromeCoverage capture(const DesktopShell &shell,const DesktopStatus &status,bool overlays,unsigned rotation){
    ChromeCoverage result;
    const auto add=[&](display::Damage area){
      for(unsigned r=0;r<(rotation&3);++r){const int x=area.x;area.x=canvas_layout::side-area.y-area.height;area.y=x;std::swap(area.width,area.height);}
      result.areas[result.count++]=area;
    };
    namespace m=lunaros::graphics::metrics;
    if(!overlays||shell.status_navigation.showing(shell.preferences.auto_hide_status)||
       status.peer_severity==DesktopStatus::Severity::Critical||status.peer_severity==DesktopStatus::Severity::Conflict)
      add({0,0,canvas_layout::side,int(status_layout::height)+m::inset});
    else add({(canvas_layout::side-m::px(40))/2-1,m::px(4)-1,m::px(40)+2,m::px(3)+2});
    const auto &home=shell.home_navigation;
    if(overlays&&home.enabled&&home.opacity*(1-home.completion)>0){
      namespace l=home_gesture_layout;
      add({int((canvas_layout::side-l::barWidth)/2)-1,int(canvas_layout::side-l::barBottom-l::barHeight)-1,int(l::barWidth)+2,int(l::barHeight)+6});
    }
    const auto &edge=shell.edge_navigation;
    if(overlays&&edge.enabled&&edge.progress>0&&std::isfinite(edge.progress)&&std::isfinite(edge.y))
      add({edge.right?canvas_layout::side-32:0,0,32,canvas_layout::side});
    return result;
  }
};
}
