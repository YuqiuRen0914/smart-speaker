#include "desktop/graphics/canvas_spec.h"
#include "desktop/navigation/home_navigation_renderer.h"
#include "desktop/navigation/page_motion.h"
#include "navigation/edge_navigation_renderer.h"
#include <array>
#include <utility>
#include "graphics/canvas_painter.h"
#include "lunaros_ui_theme.h"
#include "display/pipeline/pixel_orientation.h"

namespace lunaredu::desktop {
HomeCardPose home_card_pose(const HomeNavigation &nav,HomeTarget target){
  namespace l=home_gesture_layout;
  const float p=std::clamp(nav.progress,0.f,.9f),t=std::clamp(nav.completion,0.f,1.f);
  const float size=canvas_layout::side*(1-l::dragScaleReduction*p);
  // Card's lower edge follows finger travel after the touch slop.
  const float y=canvas_layout::center-(l::dragSpan-canvas_layout::center*l::dragScaleReduction)*p;
  const float diameter=std::clamp(target.diameter,float(lunaros::graphics::metrics::px(24)),float(lunaros::graphics::metrics::px(168)));
  const float fade=std::clamp((t-l::returnFadeStart)/(1-l::returnFadeStart),0.f,1.f);
  return {canvas_layout::center+(target.x-canvas_layout::center)*t,y+(target.y-y)*t,size+(diameter-size)*t,
    l::dragRadius*p+(diameter/2-l::dragRadius*p)*t,1-fade*fade*(3-2*fade)};
}
namespace {
lunaros::graphics::Scene *renderer_for(lunaros::graphics::Scene *renderer,
    std::unique_ptr<lunaros::graphics::Scene> &local){
  if(renderer)return renderer;
  local=std::make_unique<lunaros::graphics::Scene>();
  return local->configure(kDesktopCanvas,{0,0})?local.get():nullptr;
}
lunaros::graphics::Rect oriented(lunaros::graphics::Rect box,unsigned rotation){
  for(unsigned i=0;i<rotation%4;++i){const int x=box.x;box.x=canvas_layout::side-box.y-box.height;box.y=x;std::swap(box.width,box.height);}
  return box;
}
}
// Destination and source are separate, renderer-owned frozen RGB565 frames.
// Their existing physical orientation is shared; only the pose needs rotating.
bool compose_home_card(uint16_t *destination,const uint16_t *source,const HomeNavigation &nav,HomeTarget target,unsigned rotation,bool antialias,lunaros::graphics::Scene *renderer){
  if(!destination||!source||destination==source||!std::isfinite(nav.progress)||!std::isfinite(nav.completion))return false;
  const auto pose=home_card_pose(nav,target);if(pose.opacity<=0)return true;
  if(!std::isfinite(pose.x)||!std::isfinite(pose.y)||!std::isfinite(pose.size)||!std::isfinite(pose.radius)||pose.size<=0)return false;
  std::unique_ptr<lunaros::graphics::Scene> local;renderer=renderer_for(renderer,local);if(!renderer)return false;
  struct Context {const uint16_t *source;lunaros::graphics::Rect box;int radius;uint8_t opacity;};
  Context context{source,oriented({int(std::lround(pose.x-pose.size/2)),int(std::lround(pose.y-pose.size/2)),
    std::max(1,int(std::lround(pose.size))),std::max(1,int(std::lround(pose.size)))},rotation),
    std::max(0,int(std::lround(std::min(pose.radius,pose.size/2)))),uint8_t(std::clamp(int(std::lround(pose.opacity*255)),0,255))};
  return renderer->paint_canvas({destination,kDesktopCanvas.pixels()},antialias,&context,
    [](void *p,lunaros::graphics::CanvasPainter &canvas){const auto &c=*static_cast<Context *>(p);return canvas.image(c.box,c.source,kDesktopCanvas,c.radius,c.opacity);});
}
bool draw_home_indicator(const HomeNavigation &nav,uint16_t *pixels,unsigned rotation,bool antialias,lunaros::graphics::Scene *renderer){
  namespace l=home_gesture_layout;
  const float opacity=nav.opacity*(1-nav.completion);
  if(!nav.enabled||opacity<=0)return true;
  if(!pixels||!std::isfinite(opacity))return false;
  std::unique_ptr<lunaros::graphics::Scene> local;renderer=renderer_for(renderer,local);if(!renderer)return false;
  struct Context {lunaros::graphics::Rect box;uint8_t opacity;};
  Context context{oriented({int((canvas_layout::side-l::barWidth)/2),int(std::lround(canvas_layout::side-l::barBottom-l::barHeight+4*(1-opacity))),
      int(l::barWidth),int(l::barHeight)},rotation),uint8_t(std::clamp(int(opacity*255),0,255))};
  return renderer->paint_canvas({pixels,kDesktopCanvas.pixels()},antialias,&context,
    [](void *p,lunaros::graphics::CanvasPainter &canvas){const auto &c=*static_cast<Context *>(p);return canvas.rectangle(c.box,lunaros::graphics::theme::text,2,0,c.opacity);});
}
bool draw_edge_back_indicator(const EdgeNavigation &nav,uint16_t *pixels,unsigned rotation,bool aa,lunaros::graphics::Scene *renderer){
  if(!nav.enabled||nav.progress<=0||!std::isfinite(nav.progress)||!std::isfinite(nav.y))return true;
  if(!pixels)return false;
  std::unique_ptr<lunaros::graphics::Scene> local;renderer=renderer_for(renderer,local);if(!renderer)return false;
  struct Context {const EdgeNavigation &nav;unsigned rotation;} context{nav,rotation%4};
  return renderer->paint_canvas({pixels,kDesktopCanvas.pixels()},aa,&context,
    [](void *p,lunaros::graphics::CanvasPainter &canvas){
      const auto &c=*static_cast<Context *>(p);const auto &nav=c.nav;
      const float pull=1-std::exp(-std::clamp(nav.progress,0.f,2.f)*1.25f);
      const float depth=32*pull,half_height=25+11*pull,center=std::clamp(nav.y,60.f,188.f);
      const int first=std::max(int(status_layout::height),int(std::floor(center-half_height)));
      const int last=std::min(canvas_layout::side-int(home_gesture_layout::edgeHeight),int(std::ceil(center+half_height)));
      if(first>=last)return true;
      const uint16_t fill=nav.ready?0x426b:0x31a8,border=nav.ready?0x8cb4:0x6b8f;
      canvas.clip(oriented({nav.right?canvas_layout::side-32:0,first,32,last-first},c.rotation));
      const auto point=[&](float x,float y){
        if(nav.right)x=canvas_layout::last-x;
        for(unsigned r=0;r<c.rotation;++r){const float old=x;x=canvas_layout::last-y;y=old;}
        return std::pair{x,y};
      };
      // Keep only the product's monotonic Bezier profile. LVGL fills row
      // rectangles and rasterizes/blends the contour and arrow. There is no
      // per-pixel coverage, segment-distance or RGB565 mixing implementation.
      std::array<float,80> contour{};
      if(last-first>int(contour.size()))return false;
      for(int y=first;y<last;++y){
        const float height=std::abs(y+.5f-center)/half_height;
        float lo=0,hi=1;
        for(unsigned iteration=0;iteration<9;++iteration){
          const float t=(lo+hi)*.5f,u=1-t;
          if(u*u*u+1.56f*u*u*t+1.5f*u*t*t>height)lo=t;else hi=t;
        }
        const float t=(lo+hi)*.5f,u=1-t,edge=depth*(3*u*t*t+t*t*t);
        contour[size_t(y-first)]=edge-.5f;
        const int width=std::clamp(int(std::floor(edge)),0,32);
        if(width&&!canvas.rectangle(oriented({nav.right?canvas_layout::side-width:0,y,width,1},c.rotation),fill))return false;
      }
      for(int y=first+1;y<last;++y){
        const auto a=point(contour[size_t(y-first-1)],float(y-1));
        const auto b=point(contour[size_t(y-first)],float(y));
        if(!canvas.line(a.first,a.second,b.first,b.second,border))return false;
      }
      const float cx=depth*.55f;
      const auto a=point(cx+2.5f,center-5),b=point(cx-2.5f,center),d=point(cx+2.5f,center+5);
      const auto opacity=uint8_t(std::min(1.f,nav.progress*2)*255);
      return canvas.line(a.first,a.second,b.first,b.second,lunaros::graphics::theme::text,opacity,2,true)&&
        canvas.line(b.first,b.second,d.first,d.second,lunaros::graphics::theme::text,opacity,2,true);
    });
}
bool PageMotion::compose(uint16_t *frame,const uint16_t *source,const uint16_t *background,
      HomeTarget target,unsigned rotation,bool aa,uint32_t now,lunaros::graphics::Scene *renderer) const {
  if(!active_)return true;
  if(!frame||!source||frame==source)return false;
  std::unique_ptr<lunaros::graphics::Scene> local;renderer=renderer_for(renderer,local);if(!renderer)return false;
  float p=0;
  if(prepared_&&!renderer->animation_value(key_,uint32_t(now-started_),lunaros::graphics::design::pageEnterMs,p))return false;
  if(opening_&&background){
    if(frame==background)return false;
    std::copy_n(background,kDesktopCanvas.pixels(),frame);
    HomeNavigation nav;nav.completion=1.f-p;
    return compose_home_card(frame,source,nav,target,rotation,aa,renderer);
  }
  const int shift=int((1.f-p)*lunaros::graphics::metrics::px(12)+.5f);
  const int dx=rotation%4==1?shift:rotation%4==3?-shift:0;
  const int dy=rotation%4==0?-shift:rotation%4==2?shift:0;
  struct Context {const uint16_t *source;int dx,dy;uint8_t opacity;} context{source,dx,dy,uint8_t(170+85*p)};
  return renderer->paint_canvas({frame,kDesktopCanvas.pixels()},aa,&context,
    [](void *p,lunaros::graphics::CanvasPainter &canvas){const auto &c=*static_cast<Context *>(p);
      return canvas.rectangle({0,0,canvas_layout::side,canvas_layout::side},lunaros::graphics::theme::appbackground)&&
        canvas.image({-c.dx,-c.dy,canvas_layout::side,canvas_layout::side},c.source,kDesktopCanvas,0,c.opacity);});
}
}
