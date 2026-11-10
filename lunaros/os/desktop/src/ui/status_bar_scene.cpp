#include "desktop/ui/status_bar.h"
#include "desktop/graphics/scene_icon.h"
#include "graphics/canvas_painter.h"
#include "lunaros_ui_theme.h"
#include <algorithm>
namespace lunaredu::desktop {
namespace {
template<class Painter> bool project_status(Painter &scene,const char *title,const DesktopStatus &status,
    const StatusNavigation &nav,bool automatic,DesktopPainter painter,bool aa,const char *detail=nullptr){
  namespace m=lunaros::graphics::metrics;namespace layout=status_layout;
  namespace theme=lunaros::graphics::theme;
  using lunaros::graphics::Align;using Icon=lunaros::icons::Icon;
  constexpr uint32_t root=0xfffb0000;
  if(!status_overlay_shows_bar(status,nav,automatic))
    return scene.panel(root,{(canvas_layout::side-m::px(40))/2,m::px(4),m::px(40),m::px(3)},theme::secondary,0,0);
  if(!scene.panel(root,{0,0,layout::width,layout::height},0,0,0))return false;
  if(status.network_title)title=status.location;
  int right=layout::titleRight;
  if(detail&&*detail){
    const int width=std::min(48,painter.text_width?painter.text_width(painter.context,detail,12):48);
    if(width>0&&!scene.label(root+3,{right-width,0,width,layout::height},detail,12,theme::secondary,Align::Right,root,true))return false;
    right-=width+8;
  }
  int size=layout::textSize;
  while(size>12&&painter.text_width&&painter.text_width(painter.context,title?title:"",size)>right-layout::padding)--size;
  if(!scene.label(root+1,{layout::padding,0,right-layout::padding,layout::height},title,
      size,theme::text,Align::Left,root,true))return false;
  uint32_t next=root+10;
  const auto glyph=[&](Icon icon,layout::Rect r,uint16_t ink){return scene.glyph(next++,{r.x,r.y,r.width,r.height},icon,ink,root,aa);};
  uint16_t peer=status.peer?theme::accent:theme::secondary;
  switch(status.peer_severity){
    case DesktopStatus::Severity::Attention:peer=theme::warning;break;
    case DesktopStatus::Severity::Critical:peer=theme::danger;break;
    case DesktopStatus::Severity::Conflict:peer=0xf81f;break;
    case DesktopStatus::Severity::Normal:break;
  }
  if(!glyph(Icon::StatusWifi,layout::wifi,status.wifi?theme::text:status.wifi_connecting?theme::warning:theme::secondary)||
     !glyph(Icon::Bluetooth,layout::bluetooth,status.bluetooth?theme::accent:theme::secondary)||
     !glyph(Icon::StatusPlatform,layout::platform,status.platform?theme::success:theme::secondary)||
     !glyph(Icon::StatusPeer,layout::peer,peer))return false;
  const auto f=layout::batteryFill;const int fill=std::clamp(status.battery_percent,0,100)*f.width/100;
  if(fill&&!scene.panel(root+2,{f.x,f.y,fill,f.height},status.battery_percent<=15?theme::danger:theme::success,root,0))return false;
  return glyph(Icon::StatusBattery,layout::battery,theme::secondary)&&
    (status.battery_percent<0?glyph(Icon::StatusPower,layout::batterySymbol,theme::text):
      !status.charging||glyph(Icon::StatusCharging,layout::batterySymbol,theme::text));
}
struct SceneStatus {
  lunaros::graphics::Scene &scene;
  bool panel(uint32_t id,lunaros::graphics::Rect r,uint16_t ink,uint32_t parent,int radius){return scene.panel(id,r,ink,parent,radius);}
  bool label(uint32_t id,lunaros::graphics::Rect r,const char *text,unsigned size,uint16_t ink,lunaros::graphics::Align align,uint32_t parent,bool dots){return scene.label(id,r,text,size,ink,align,parent,dots);}
  bool glyph(uint32_t id,lunaros::graphics::Rect r,lunaros::icons::Icon icon,uint16_t ink,uint32_t parent,bool aa){return scene_icon(scene,id,r,icon,ink,parent,aa);}
};
struct CanvasStatus {
  lunaros::graphics::CanvasPainter &canvas;
  bool panel(uint32_t,lunaros::graphics::Rect r,uint16_t ink,uint32_t,int radius){return canvas.rectangle(r,ink,radius);}
  bool label(uint32_t,lunaros::graphics::Rect r,const char *text,unsigned size,uint16_t ink,lunaros::graphics::Align align,uint32_t,bool dots){return canvas.label(r,text,size,ink,align,dots);}
  bool glyph(uint32_t,lunaros::graphics::Rect r,lunaros::icons::Icon icon,uint16_t ink,uint32_t,bool){return canvas.mask(r,lunaros::icons::builtin(icon),lunaros::icons::bounds(icon),ink,255,1+uint64_t(unsigned(icon)));}
};
}
bool build_status_overlay(lunaros::graphics::Scene &scene,const char *title,const DesktopStatus &status,
    const StatusNavigation &nav,bool automatic,DesktopPainter painter,bool aa){
  SceneStatus target{scene};return project_status(target,title,status,nav,automatic,painter,aa);
}
bool build_status_overlay(lunaros::graphics::CanvasPainter &canvas,const char *title,const DesktopStatus &status,
    const StatusNavigation &nav,bool automatic,const char *detail){
  DesktopPainter metrics{};metrics.context=&canvas;
  metrics.text_width=[](void *p,const char *text,int size){return static_cast<lunaros::graphics::CanvasPainter *>(p)->text_bounds(text,unsigned(size)).width;};
  CanvasStatus target{canvas};return project_status(target,title,status,nav,automatic,metrics,true,detail);
}
bool paint_status_overlay(lunaros::graphics::Scene &scene,lunaros::graphics::CanvasSpec spec,uint16_t *pixels,
    const char *title,const DesktopStatus &status,const StatusNavigation &nav,bool automatic,bool aa,const char *detail){
  if(!pixels||!spec.valid())return false;
  struct Context {const char *title;const DesktopStatus &status;const StatusNavigation &nav;bool automatic;const char *detail;} context{title,status,nav,automatic,detail};
  return scene.paint_canvas(spec,{pixels,spec.pixels()},aa,&context,[](void *p,lunaros::graphics::CanvasPainter &canvas){
    const auto &c=*static_cast<Context *>(p);
    return build_status_overlay(canvas,c.title,c.status,c.nav,c.automatic,c.detail);
  });
}

}
