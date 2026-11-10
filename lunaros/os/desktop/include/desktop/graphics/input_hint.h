#pragma once
#include "graphics/scene.h"
#include "lunaros_ui_theme.h"
#include "desktop/ui/text_layout.h"
#include <array>

namespace lunaredu::lunaros::graphics {
enum class PhysicalKey { None, A, B };
struct HintPart { const char *text; PhysicalKey key=PhysicalKey::None; };
using HintKeyPainter=void (*)(void *,Rect,PhysicalKey);

// Display-only physical input hints. No action IDs, events or per-frame allocation.
// Products supply the intent; the OS owns typography and aligned key placement.
template<size_t count>
bool paint_input_hint(desktop::DesktopPainter painter,uint16_t *pixels,Rect box,
    const std::array<HintPart,count> &parts,HintKeyPainter key_painter,void *context,
    uint16_t ink=theme::secondary,bool antialias=true) {
  static_assert(count>0&&count<=12,"Physical hints have a bounded number of parts");
  if(!pixels||!painter.text||!painter.text_width||!key_painter||box.height<design::keyDiameter)return false;
  std::array<int,count> widths{};
  int width=int(count-1)*design::hintGap;
  for(size_t i=0;i<count;++i){
    if(!parts[i].text)return false;
    widths[i]=parts[i].key==PhysicalKey::None?painter.text_width(painter.context,parts[i].text,desktop::typography::secondary.size):design::keyDiameter;
    width+=widths[i];
  }
  if(width>box.width)return false; // Never partially render an instruction that does not fit.
  int x=box.x+(box.width-width)/2;
  for(size_t i=0;i<count;++i){
    const auto &part=parts[i];
    if(part.key!=PhysicalKey::None)key_painter(context,{x,box.y+(box.height-design::keyDiameter)/2,design::keyDiameter,design::keyDiameter},part.key);
    else {
      const auto origin=desktop::text_origin(painter,part.text,desktop::typography::secondary.size,x,box.y,widths[i],box.height);
      painter.text(painter.context,pixels,origin.x,origin.y,part.text,desktop::typography::secondary.size,ink,antialias);
    }
    x+=widths[i]+design::hintGap;
  }
  return true;
}
} // namespace lunaredu::lunaros::graphics
