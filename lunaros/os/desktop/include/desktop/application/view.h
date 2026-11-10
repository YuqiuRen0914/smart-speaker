#pragma once
#include "app/execution/application_session.h"
#include "desktop/graphics/canvas.h"
namespace lunaredu::lunaros::apps {
inline void paint_application_frame(graphics::canvas::Surface surface,const DrawList &frame) {
  for(const auto &c:frame){
    if(c.op!=3)surface.rect({c.x,c.y,c.width,c.height},c.color);
    else surface.label({c.x,c.y,c.width,c.height},c.text.c_str(),c.size,c.color,c.align!=0);
  }
}
}
