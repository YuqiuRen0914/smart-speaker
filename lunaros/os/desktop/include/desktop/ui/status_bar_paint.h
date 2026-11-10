#pragma once
#include "desktop/ui/status_bar.h"
#include "lunaros_ui_theme.h"
#include "graphics/icon_draw.h"
#include <algorithm>
#include <cstring>

namespace lunaredu::desktop {
// Direct ARGB Pet overlay policy on its independent render owner. RGB565
// Desktop/raw App chrome uses status_bar_scene's synchronous LVGL projection.
// This adapter keeps the product font port and never shares LVGL objects.
template<class Canvas>
void paint_status_bar(Canvas &c, const char *title, const DesktopStatus &status,
                      DesktopPainter painter, const char *detail=nullptr) {
  namespace layout=status_layout;
  namespace theme=lunaros::graphics::theme;
  using Icon=lunaros::icons::Icon;
  if(status.network_title)title=status.location;
  const auto bounds=[&](const char *s,int size) {
    return painter.text_bounds ? painter.text_bounds(painter.context,s,size) :
      DesktopPainter::TextBounds{0,0,painter.text_width?painter.text_width(painter.context,s,size):0,size};
  };
  const auto fit=[&](const char *s,int width,int size,char (&out)[96]) {
    if(!s)s="";
    if(!painter.text_width||painter.text_width(painter.context,s,size)<=width)return s;
    size_t length=0;out[0]=0;
    for(const char *p=s;*p;){
      const auto first=static_cast<unsigned char>(*p);
      const size_t bytes=first<128?1:(first&0xe0)==0xc0?2:(first&0xf0)==0xe0?3:4;
      if(length+bytes+4>=sizeof(out)||std::strlen(p)<bytes)break;
      std::memcpy(out+length,p,bytes);std::memcpy(out+length+bytes,"...",4);
      if(painter.text_width(painter.context,out,size)>width)break;
      length+=bytes;p+=bytes;
    }
    std::memcpy(out+length,"...",4);return static_cast<const char *>(out);
  };
  const auto text=[&](int x,const char *s,int size,uint16_t color) {
    const auto b=bounds(s,size);c.text(x-b.left,(layout::height-b.height)/2-b.top,s,size,color);
  };
  c.rect(0,0,layout::width,layout::height,0);
  int right=layout::titleRight;
  if(detail&&*detail){char clipped[96]{};const auto s=fit(detail,48,12,clipped);
    const int x=right-bounds(s,12).width;text(x,s,12,theme::secondary);right=x-8;}
  // Keep complete IPv4 addresses readable before resorting to ellipsis.
  int size=layout::textSize;
  while(size>12&&painter.text_width&&painter.text_width(painter.context,title?title:"",size)>right-layout::padding)--size;
  char clipped[96]{};text(layout::padding,fit(title,right-layout::padding,size,clipped),size,theme::text);
  const auto glyph=[&](Icon id,layout::Rect r,uint16_t color){c.glyph_box(id,r.x,r.y,r.width,r.height,color);};
  glyph(Icon::StatusWifi,layout::wifi,status.wifi?theme::text:status.wifi_connecting?theme::warning:theme::secondary);
  glyph(Icon::Bluetooth,layout::bluetooth,status.bluetooth?theme::accent:theme::secondary);
  glyph(Icon::StatusPlatform,layout::platform,status.platform?theme::success:theme::secondary);
  uint16_t peer=status.peer?theme::accent:theme::secondary;
  switch(status.peer_severity){
    case DesktopStatus::Severity::Attention:peer=theme::warning;break;
    case DesktopStatus::Severity::Critical:peer=theme::danger;break;
    case DesktopStatus::Severity::Conflict:peer=0xf81f;break;
    case DesktopStatus::Severity::Normal:break;
  }
  glyph(Icon::StatusPeer,layout::peer,peer);
  const auto f=layout::batteryFill;
  if(status.battery_percent>=0)c.rect(f.x,f.y,std::clamp(status.battery_percent,0,100)*f.width/100,f.height,status.battery_percent<=15?theme::danger:theme::success);
  glyph(Icon::StatusBattery,layout::battery,theme::secondary);
  if(status.battery_percent<0)glyph(Icon::StatusPower,layout::batterySymbol,theme::text);
  else if(status.charging)glyph(Icon::StatusCharging,layout::batterySymbol,theme::text);
}
}
