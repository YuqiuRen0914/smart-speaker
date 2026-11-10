#include "desktop/navigation/snapshot.h"
#include "diagnostics/runtime_work_profile.h"
#include "desktop/graphics/canvas_spec.h"
#include "graphics/compact_honeycomb.h"
#include "compiled_wallpaper.h"
#include "desktop/navigation/home_navigation_renderer.h"
#include "desktop/shell/shell.h"
#include "desktop/graphics/icon_resource.h"
#include "lunaros_ui_theme.h"
#include "display/pipeline/pixel_orientation.h"
#include "graphics/color.h"
#include "desktop/ui/status_bar.h"
#include "navigation/edge_navigation_renderer.h"
#include "lunaros_ui_typography.generated.h"
#include "graphics/canvas_painter.h"
#include "image_rgb565_transform.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <array>
#ifdef ESP_PLATFORM
#include <esp_attr.h>
#include <esp_log.h>
#endif

namespace lunaredu::desktop {
namespace {
namespace theme=lunaros::graphics::theme;
constexpr uint16_t white=theme::text,muted=theme::secondary,accent=theme::accent;
// Product positioning/fade policy stays here; LVGL owns all primitive rasterization.
struct Canvas {
  lunaros::graphics::CanvasPainter &raster;
  DesktopPainter painter{};
  bool antialias=true;
  int clip_top=0;
  explicit Canvas(lunaros::graphics::CanvasPainter &raster,bool aa):raster(raster),antialias(aa){
    painter.context=&raster;
    painter.text_width=[](void *p,const char *value,int size){return static_cast<lunaros::graphics::CanvasPainter *>(p)->text_bounds(value,unsigned(size)).width;};
    painter.text_bounds=[](void *p,const char *value,int size){const auto b=static_cast<lunaros::graphics::CanvasPainter *>(p)->text_bounds(value,unsigned(size));return DesktopPainter::TextBounds{b.x,b.y,b.width,b.height};};
  }
  void clip(){raster.clip({0,clip_top,canvas_layout::side,canvas_layout::side-clip_top});}
  void rect(int x,int y,int w,int h,uint16_t ink){clip();raster.rectangle({x,y,w,h},ink);}
  void rounded(HomeBox b,int radius,uint16_t ink){clip();raster.rectangle({b.x,b.y,b.width,b.height},ink,radius);}
  void circle(float x,float y,float radius,uint16_t ink,bool outline=false){
    if(radius<=0)return;
    const int left=int(std::floor(x-radius)),top=int(std::floor(y-radius)),side=std::max(1,int(std::ceil(radius*2)));
    clip();raster.rectangle({left,top,side,side},ink,side/2,outline?2:0);
  }
  void line(float x1,float y1,float x2,float y2,uint16_t ink,uint8_t opacity){
    if constexpr(lunaros::graphics::metrics::native){clip();raster.line(x1,y1,x2,y2,ink,opacity);return;}
    // Constant-opacity clipping strips preserve the product edge/radial fade.
    // Drawing the original whole line in every strip avoids segment-end seams;
    // LVGL performs line coverage, clipping and blending, including non-AA mode.
    const float dx=x2-x1,dy=y2-y1;const bool horizontal=std::abs(dx)>=std::abs(dy);
    if(!std::isfinite(dx)||!std::isfinite(dy)||(!dx&&!dy))return;
    if(std::max(x1,x2)+2<0||std::min(x1,x2)-2>=canvas_layout::side||
       std::max(y1,y2)+2<clip_top||std::min(y1,y2)-2>=canvas_layout::side)return;
    const float a=horizontal?x1:y1,b=horizontal?x2:y2;
    const int lower=int(std::max(float(horizontal?0:clip_top),std::floor(std::min(a,b))-1));
    const int upper=int(std::min(float(canvas_layout::side),std::ceil(std::max(a,b))+2));
    for(int band=lower;band<upper;band+=8){
      const int count=std::min(8,upper-band);const float t=std::clamp((band+count*.5f-a)/(b-a),0.f,1.f);
      const int x=std::clamp(int(std::lround(x1+dx*t)),0,canvas_layout::last),y=std::clamp(int(std::lround(y1+dy*t)),0,canvas_layout::last);
      const int cx=x-canvas_layout::center,cy=y-(canvas_layout::side+clip_top)/2;
      const int radial=255-std::clamp((cx*cx+cy*cy-6400)*160/14400,0,160);
      const auto fade=std::min(compact_honeycomb::edge_fade[size_t(x)],compact_honeycomb::edge_fade[size_t(y)])*radial/255;
      raster.clip(horizontal?lunaros::graphics::Rect{band,clip_top,count,canvas_layout::side-clip_top}:lunaros::graphics::Rect{0,band,canvas_layout::side,count});
      raster.line(x1,y1,x2,y2,ink,uint8_t(unsigned(opacity)*unsigned(fade)/255));
    }
  }
  void text(int x,int y,const char *value,int size=typography::label.size,uint16_t ink=white){clip();raster.text(x,y,value,unsigned(size),ink);}
  void clipped_text(HomeBox box,int x,int y,const char *value,int size,uint16_t ink=white){
    const int top=std::max(box.y,clip_top),bottom=std::min(box.y+box.height,canvas_layout::side);
    raster.clip({box.x,top,box.width,std::max(0,bottom-top)});raster.text(x,y,value,unsigned(size),ink);
  }
  void centered(int x,int y,const char *value,int size=typography::label.size,uint16_t ink=white){const auto b=raster.text_bounds(value,unsigned(size));text(x-b.width/2,y,value,size,ink);}
  void glyph_box(lunaros::icons::Icon id,int x,int y,int width,int height,uint16_t ink=white,bool fitted=false,uint8_t opacity=255){
    if(!opacity)return;
    const auto view=lunaros::icons::builtin(id);
    const auto crop=fitted?lunaros::icons::bounds(id):lunaros::assets::MaskBounds{0,0,view.width,view.height};
    clip();raster.mask({x,y,width,height},view,crop,ink,opacity,1+uint64_t(unsigned(id)));
  }
  void icon(DesktopIcon id,int x,int y,int diameter,uint8_t opacity=255,uint16_t ink=white){glyph_box(desktop_icon(id),x-diameter/2,y-diameter/2,diameter,diameter,ink,true,opacity);}
};

[[maybe_unused]] void paint_compact(Canvas &c,const DesktopShell &s){
  const auto &h=s.compact;if(!h.layout)return;const int top=s.launcher_top();
  const auto text=[&](HomeBox clip,int x,int y,const char *value,int size,uint16_t color=white){
    if(!value)return;
    DesktopPainter::TextBounds b{};
    if(c.painter.text_bounds)for(int attempt=0;attempt<3;++attempt){b=c.painter.text_bounds(c.painter.context,value,size);if(b.width<=clip.x+clip.width-x&&b.height<=clip.y+clip.height-y)break;size=std::max(1,std::min({size-1,size*(clip.x+clip.width-x)/std::max(1,b.width),size*(clip.y+clip.height-y)/std::max(1,b.height)}));}
    if(c.painter.text_bounds)b=c.painter.text_bounds(c.painter.context,value,size);
    c.clipped_text(clip,x-b.left,y-b.top,value,size,color);
  };
  const HomeBox header{6,top,228,28};
  if(h.mode==CompactMode::Desktop){
    bool any=false;for(size_t i=0;i<s.count;++i)any|=s.launcher_visible(i);
    if(!any)text({16,90,208,60},42,105,"长按恢复应用",14,muted);
  }else if(h.mode==CompactMode::Edit){
    c.rounded({4,top,232,24},8,uint16_t(0x2104));
    const auto selected=s.find_app(h.selected.c_str());text(header,10,top+5,selected<s.count?s.apps[selected].name:"整理桌面",12);text(header,186,top+5,"完成",12,accent);
    if(h.dragging&&selected<s.count){c.circle(float(h.last_x),float(h.last_y),21,white);c.circle(float(h.last_x),float(h.last_y),19,s.apps[selected].color);c.icon(s.apps[selected].icon,h.last_x,h.last_y,24);}
  }else{
    const char *title=h.mode==CompactMode::Menu?"桌面管理":h.mode==CompactMode::AllApps?"所有应用":h.mode==CompactMode::Appearance?"桌面外观":h.mode==CompactMode::Uninstall?"卸载应用":"应用操作";
    text(header,8,top+6,"返回",12,muted);text(header,74,top+6,title,13);
    if(h.mode==CompactMode::AllApps)text(header,194,top+6,"更多",12,muted);
    const auto row=[&](unsigned index,const char *label,bool danger=false){const HomeBox box{8,top+32+int(index)*56,224,48};c.rounded(box,12,index==h.row?uint16_t(0x3186):uint16_t(0x2104));text({box.x+12,box.y+8,box.width-24,32},20,box.y+16,label,14,danger?theme::danger:white);};
    if(h.mode==CompactMode::Menu){row(0,"所有应用");row(1,"壁纸与动效");}
    else if(h.mode==CompactMode::AllApps){for(unsigned i=0;i<3;++i){const auto index=h.page*3+i;if(index>=s.count)break;const auto &app=s.apps[index];const HomeBox box{8,top+32+int(i)*56,224,48};c.rounded(box,12,i==h.row?uint16_t(0x3186):uint16_t(0x2104));c.circle(31,box.y+24,15,app.color);if(app.icon_resource&&*app.icon_resource)c.glyph_box(lunaros::icons::find(app.icon_resource),21,box.y+14,20,20,white,true);else c.icon(app.icon,31,box.y+24,20);text(box,58,box.y+8,app.name,14);text(box,58,box.y+29,h.visible(app.id)?"长按管理":"已移除 · 长按恢复",10,muted);}}
    else if(h.mode==CompactMode::Appearance){row(0,"切换内置壁纸");row(1,s.preferences.animations?"动效 · 已开启":"动效 · 已关闭");row(2,"更多显示设置");}
    else if(h.mode==CompactMode::Uninstall){row(0,"确认卸载",true);row(1,"取消");text({12,top+160,216,45},26,top+178,"保留应用数据",12,muted);}
    else{const auto index=s.find_app(h.selected.c_str());if(index<s.count){const auto &app=s.apps[index];if(h.mode==CompactMode::ItemActions){row(0,"移动位置");row(1,"从桌面移除",true);}else{row(0,h.visible(app.id)?"从桌面移除":"恢复到桌面");row(1,"打开应用");}row(2,app.uninstallable?"卸载应用":h.mode==CompactMode::ItemActions?"完成编辑":"返回应用列表",app.uninstallable);}}
  }
  if(!h.error.empty()){const HomeBox clip{8,216,224,22};c.rounded(clip,8,uint16_t(0x2104));text(clip,12,221,h.error.c_str(),12,theme::danger);}
}

void paint_home(Canvas &c,const DesktopShell &s,const DesktopStatus &status){
  const auto &h=s.home;if(!h.layout)return;
  namespace metrics=lunaros::graphics::metrics;
  const auto font=[&](int size){return int(metrics::scaled_text_size(metrics::font(size>=48?32:size>=18?12:10),s.preferences.text_size));};
  const auto text=[&](int x,int y,const char *value,int size=16,uint16_t color=white){c.text(x,y,value,font(size),color);};
  const auto centered=[&](int x,int y,const char *value,int size=16,uint16_t color=white){c.centered(x,y,value,font(size),color);};
  const auto item_icon=[&](size_t index,HomeBox b,bool selected){
    const auto &a=s.apps[index];const int label_size=std::min(font(16),(b.width+8)/4);
    const auto bounds=c.painter.text_bounds?c.painter.text_bounds(c.painter.context,a.name,label_size):DesktopPainter::TextBounds{0,0,0,label_size};
    const int icon_height=b.height-(s.preferences.show_labels?bounds.height+8:0);
    const int diameter=std::min({metrics::launcher_icon,b.width-16,icon_height});
    const int x=b.x+b.width/2,y=b.y+icon_height/2;
    if(selected)c.rounded({x-diameter/2-3,y-diameter/2-3,diameter+6,diameter+6},18,white);
    c.rounded({x-diameter/2,y-diameter/2,diameter,diameter},16,a.color);
    if(a.icon_resource&&*a.icon_resource)c.glyph_box(lunaros::icons::find(a.icon_resource),x-diameter/3,y-diameter/3,diameter*2/3,diameter*2/3,white,true);
    else c.icon(a.icon,x,y,diameter*2/3);
    if(s.has_badge(index))c.circle(x+diameter*.42f,y-diameter*.42f,5,theme::danger);
    if(s.preferences.show_labels){
      const int width=c.painter.text_width?c.painter.text_width(c.painter.context,a.name,label_size):0;
      c.clipped_text({b.x-4,b.y,b.width+8,b.height},x-width/2,b.y+b.height-1-bounds.top-bounds.height,a.name,label_size);
    }
  };
  if(h.mode==HomeMode::Desktop||h.mode==HomeMode::Edit){
    for(size_t i=0;i<h.layout->items.size();++i){const auto &item=h.layout->items[i];if(!h.visible(item.page)||int(i)==h.drag_item)continue;
      const auto b=h.box(s,item);
      if(item.kind==HomeItemKind::Application){const auto index=s.find_app(item.id.c_str());if(index<s.count)item_icon(index,b,h.mode==HomeMode::Edit&&h.selected==int(i));continue;}
      const auto *definition=h.definition(item.id.c_str());if(!definition)continue;
      const auto *content=h.content(item.id.c_str());const auto fill=content?content->color:uint16_t(0x2945);
      c.rounded(b,18,fill);
      const auto widget_text=[&](int x,int y,const char *value,int size=16,uint16_t color=white){
        int actual=font(size);DesktopPainter::TextBounds bounds{};
        const int width=b.x+b.width-12-x,height=b.y+b.height-8-y;
        if(width<=0||height<=0)return;
        // Cached glyph measurements also bound large text in narrow widgets.
        // Grid geometry and user scale variants remain compile-time tables.
        if(c.painter.text_bounds)for(int attempt=0;attempt<3;++attempt){
          bounds=c.painter.text_bounds(c.painter.context,value,actual);
          if(bounds.width<=width&&bounds.height<=height)break;
          actual=std::max(1,std::min({actual-1,actual*width/std::max(1,bounds.width),actual*height/std::max(1,bounds.height)}));
        }
        if(c.painter.text_bounds)bounds=c.painter.text_bounds(c.painter.context,value,actual);
        c.clipped_text({b.x+12,b.y+8,b.width-24,b.height-16},x-bounds.left,y-bounds.top,value,actual,color);
      };
      widget_text(b.x+16,b.y+12,definition->title,14,muted);
      if(item.id=="system.connection"){
        widget_text(b.x+16,b.y+36,status.wifi?"网络已连接":"网络未连接",16);
        if(b.height>85)widget_text(b.x+16,b.y+62,status.platform?"平台在线":"平台未连接",14,muted);
      }else if(content&&!content->available){widget_text(b.x+16,b.y+36,"暂不可用",16,muted);}
      else if(content){
        widget_text(b.x+16,b.y+35,content->value,item.id=="system.clock"&&item.height==2?48:18);
        if(b.height>110)widget_text(b.x+16,b.y+b.height-34,content->detail,14,muted);
      }else widget_text(b.x+16,b.y+36,"等待状态",16,muted);
      if(h.mode==HomeMode::Edit&&h.selected==int(i)){c.rect(b.x,b.y,b.width,2,white);c.rect(b.x,b.y+b.height-2,b.width,2,white);}
    }
    if(h.dragging&&h.drag_item>=0){const auto &item=h.layout->items[size_t(h.drag_item)];auto b=h.box(s,item,false);b.x=int(h.drag_x)-b.width/2;b.y=int(h.drag_y)-b.height/2;
      const auto target=h.box(s,h.drop_target(int(h.drag_x),int(h.drag_y)),false);
      c.rect(target.x,target.y,target.width,2,accent);c.rect(target.x,target.y+target.height-2,target.width,2,accent);
      c.rect(target.x,target.y,2,target.height,accent);c.rect(target.x+target.width-2,target.y,2,target.height,accent);
      if(item.kind==HomeItemKind::Application){const auto index=s.find_app(item.id.c_str());if(index<s.count)item_icon(index,b,true);}
      else{c.rounded(b,18,accent);centered(b.x+b.width/2,b.y+b.height/2,"移动组件",16);}
    }
    if(h.mode==HomeMode::Edit){
      constexpr const char *labels[]={"组件","页面","外观","完成"};
      for(int i=0;i<4;++i){c.rounded({i*(canvas_layout::side/4)+8,canvas_layout::side-40,canvas_layout::side/4-16,32},12,i==3?accent:uint16_t(0x2104));centered(i*(canvas_layout::side/4)+canvas_layout::side/8,canvas_layout::side-34,labels[i],16);}
    }else if(h.layout->pages>1){const int start=canvas_layout::side/2-int(h.layout->pages-1)*6;for(unsigned p=0;p<h.layout->pages;++p)c.circle(float(start+int(p)*12),float(canvas_layout::side-14),p==h.page?3:2,p==h.page?white:muted);}
  }else if(h.mode==HomeMode::AllApps){
    const unsigned size=h.layout->columns*h.layout->rows;
    for(size_t i=0;i<s.count;++i){HomeItem item{s.apps[i].id};item.page=uint8_t(i/size);if(!h.visible(item.page))continue;item.column=uint8_t(i%size%h.layout->columns);item.row=uint8_t(i%size/h.layout->columns);item_icon(i,h.box(s,item),false);}
    centered(canvas_layout::side/2,canvas_layout::side-24,"全部应用 · 长按添加到桌面",14,muted);
  }else{
    const int top=s.launcher_top();const char *title=h.mode==HomeMode::Widgets?"添加小组件":h.mode==HomeMode::Pages?"管理页面":h.mode==HomeMode::PageActions?"页面顺序":h.mode==HomeMode::Appearance?"桌面外观":h.mode==HomeMode::Uninstall?"卸载应用":h.mode==HomeMode::DrawerActions?"应用操作":"桌面项目";
    c.rounded({16,top+8,canvas_layout::side-32,44},14,uint16_t(0x2104));text(30,top+18,"返回",16);centered(canvas_layout::side/2,top+18,title,18);
    if(h.mode==HomeMode::Widgets||h.mode==HomeMode::Pages)text(canvas_layout::side-72,top+18,"下一页",14);
    const auto row=[&](int i,const char *label,bool danger=false){c.rounded({24,top+64+i*52,canvas_layout::side-48,44},12,uint16_t(0x2104));text(40,top+75+i*52,label,18,danger?theme::danger:white);};
    if(h.mode==HomeMode::Widgets){size_t n=0;const auto *system=system_home_widgets(n);for(unsigned i=0;i<6;++i){const auto index=h.panel_page*6+i;const auto *d=index<n?system+index:index<n+h.widget_count?h.widgets+(index-n):nullptr;if(d)row(int(i),d->title);}}
    else if(h.mode==HomeMode::Pages){for(unsigned i=0;i<4;++i){const unsigned index=h.panel_page*4+i;if(index<h.layout->pages){char label[48];std::snprintf(label,sizeof(label),"第 %u 屏%s",index+1,index==h.page?" · 当前":"");row(int(i),label);text(canvas_layout::side-80,top+75+int(i)*52,"排序",16,muted);}}row(4,"添加空白页面");row(5,"移除当前页面",true);}
    else if(h.mode==HomeMode::PageActions){row(0,"向前移动一屏");row(1,"向后移动一屏");row(2,"移除这一屏",true);row(3,"自动整理全部页面");}
    else if(h.mode==HomeMode::DrawerActions&&h.drawer_selected>=0&&size_t(h.drawer_selected)<s.count){row(0,"添加到桌面");row(1,"打开应用");if(s.apps[h.drawer_selected].uninstallable)row(2,"卸载应用",true);}
    else if(h.mode==HomeMode::Appearance){row(0,"3 × 3 · 舒展");row(1,"4 × 4 · 标准");row(2,"5 × 4 · 紧凑");row(3,"5 × 5 · 更多");char label[64];std::snprintf(label,sizeof(label),"壁纸 · %s",wallpaper_name(s.preferences.wallpaper));row(4,label);row(5,s.preferences.animations?"界面动效 · 开启":"界面动效 · 关闭");}
    else if(h.mode==HomeMode::ItemActions&&h.selected>=0&&size_t(h.selected)<h.layout->items.size()){
      const auto &item=h.layout->items[size_t(h.selected)];row(0,"移动位置");row(1,"从桌面移除",true);
      if(item.kind==HomeItemKind::Widget){const auto *d=h.definition(item.id.c_str());if(d&&d->alternate_height)row(2,"切换组件尺寸");}else{const auto index=s.find_app(item.id.c_str());if(index<s.count&&s.apps[index].uninstallable)row(2,"卸载应用",true);}
    }else if(h.mode==HomeMode::Uninstall){row(0,"确认卸载 · 保留应用数据",true);row(1,"取消");}
  }
  if(!h.error.empty())centered(canvas_layout::side/2,canvas_layout::side-68,h.error.c_str(),14,theme::danger);
}

}
static bool paint_wallpaper(const DesktopShell &s,uint16_t *pixels){
  if(!cleanup_rgb565_transforms()){std::fill_n(pixels,kDesktopCanvas.pixels(),uint16_t(0));return false;}
  const auto style=s.preferences.wallpaper;
  if(style==DesktopWallpaper::Image&&s.wallpaper_image){
    const auto &image=*s.wallpaper_image;
    if(image.width&&image.height&&image.pixels.size()==size_t(image.width)*image.height){
      // Crop in source coordinates, bounding even very thin images to the compiled canvas.
      const uint16_t side=std::min(image.width,image.height);
      std::string error;
      const bool complete=transform_rgb565_image(image.pixels.data(),image.pixels.size(),image.width,image.height,
          {uint16_t((image.width-side)/2),uint16_t((image.height-side)/2),side,side},pixels,kDesktopCanvas.pixels(),
          canvas_layout::side,canvas_layout::side,true,error);
      if(!complete)std::fill_n(pixels,kDesktopCanvas.pixels(),uint16_t(0));
      return complete;
    }
  }
  if(style==DesktopWallpaper::Solid||style==DesktopWallpaper::Image){std::fill_n(pixels,kDesktopCanvas.pixels(),uint16_t(0));return true;}
  const unsigned slot=style==DesktopWallpaper::Ocean?0:style==DesktopWallpaper::Aurora?1:2;
  for(int y=0;y<canvas_layout::side;++y){
    const auto row=compiled_wallpaper::rows[slot][y];
    for(int x=0;x<canvas_layout::side;++x){
      const int distance=std::min(canvas_layout::last,std::abs(x-row.center));
      pixels[y*canvas_layout::side+x]=lunaros::raster::blend_rgb565(row.base,compiled_wallpaper::glow[slot],compiled_wallpaper::opacity[distance]);
    }
  }
  return true;
}
static void draw_wallpaper(const DesktopShell &s,uint16_t *pixels){
  // A wallpaper is immutable, independent of camera, focus and overlays.
  // Keep only the fitted raster; a weak source reference cannot retain a
  // replaced full-resolution user image after its last consumer exits.
#ifdef ESP_PLATFORM
  EXT_RAM_BSS_ATTR
#endif
  static std::array<uint16_t,kDesktopCanvas.pixels()> raster{};
  static bool valid=false,aa=false;
  static DesktopWallpaper style=DesktopWallpaper::Solid;
  static std::weak_ptr<const DesktopImage> image;
  const bool same_image=!image.owner_before(s.wallpaper_image)&&!s.wallpaper_image.owner_before(image);
  if(!valid||style!=s.preferences.wallpaper||aa!=s.preferences.antialias||!same_image){
    valid=paint_wallpaper(s,raster.data());style=s.preferences.wallpaper;aa=s.preferences.antialias;image=s.wallpaper_image;
  }
  // Solid is exactly zero RGB565. Avoid a full PSRAM read (and cache churn)
  // merely to copy zeros; keep the same source-transition/cleanup admission.
  if(style==DesktopWallpaper::Solid)std::memset(pixels,0,raster.size()*sizeof(uint16_t));
  else std::copy_n(raster.data(),raster.size(),pixels);
}
bool draw_launcher(const DesktopShell &s,const DesktopStatus &status,uint16_t *pixels,DesktopPainter painter,bool indicator,uint16_t *navigation_source,lunaros::graphics::Scene *renderer) {
  if(indicator&&navigation_source&&s.home_navigation.progress>0){
    NavigationSnapshot snapshot(s);
    if(!snapshot)return false;
    snapshot->surface=s.navigation_surface;snapshot->home_navigation.reset();
    if(!draw_launcher(*snapshot,status,navigation_source,painter,false,nullptr,renderer))return false;
    snapshot->surface=DesktopSurface::Launcher;snapshot->settle_motion();
    if(!draw_launcher(*snapshot,status,pixels,painter,false,nullptr,renderer))return false;
    const auto tile=snapshot->tile(snapshot->focus);
    if(!compose_home_card(pixels,navigation_source,s.home_navigation,{tile.x,tile.y,tile.diameter},s.preferences.rotation,s.preferences.antialias,renderer))return false;
    return draw_home_indicator(s.home_navigation,pixels,s.preferences.rotation,s.preferences.antialias,renderer);
  }
  if(!pixels)return false;
  static system::RuntimeWorkProfile<5> profile;
  profile.begin();
  draw_wallpaper(s,pixels);profile.mark(0);
  const auto policy=resolve_render_policy(s.preferences.antialias,{},true);
  std::unique_ptr<lunaros::graphics::Scene> local;
  if(!renderer){
    local=std::make_unique<lunaros::graphics::Scene>();
    if(!local->configure(kDesktopCanvas,{0,64*1024}))return false;
    renderer=local.get();
  }
  struct Context {const DesktopShell &shell;const DesktopStatus &status;bool antialias;system::RuntimeWorkProfile<5> &profile;} context{s,status,policy.antialias,profile};
  if(!renderer->paint_canvas({pixels,kDesktopCanvas.pixels()},policy.antialias,&context,
      [](void *opaque,lunaros::graphics::CanvasPainter &raster){
  const auto &context=*static_cast<Context *>(opaque);const auto &s=context.shell;const auto &status=context.status;
  Canvas c{raster,context.antialias};c.clip_top=s.launcher_top();
  context.profile.mark(1);
  if(lunaros::graphics::metrics::native&&s.surface==DesktopSurface::Launcher&&s.home.enabled)paint_home(c,s,status);
  else if(!lunaros::graphics::metrics::native&&s.compact.enabled&&s.surface==DesktopSurface::Launcher&&s.compact.mode!=CompactMode::Desktop&&s.compact.mode!=CompactMode::Edit)paint_compact(c,s);
  else if(s.surface==DesktopSurface::Launcher && s.apps) {
    if constexpr(!lunaros::graphics::metrics::native){
      if(s.preferences.honeycomb_lines){
        const float x=canvas_layout::center-s.camera.x,y=(canvas_layout::side+s.launcher_top())/2.f-s.camera.y;
        for(size_t i=0;i<compact_honeycomb::soft_borders.count;++i){const auto edge=compact_honeycomb::soft_borders.segments[i];
          c.line(x+edge.a.x,y+edge.a.y,x+edge.b.x,y+edge.b.y,muted,48);
        }
      }
    }
    context.profile.mark(2);
    for(size_t i=0;i<s.count;++i) {auto t=s.tile(i);if(!t.visible||(!lunaros::graphics::metrics::native&&s.compact.enabled&&s.compact.dragging&&s.compact.selected==s.apps[i].id))continue;int x=static_cast<int>(t.x),y=static_cast<int>(t.y),r=static_cast<int>(t.diameter/2);
      if(i==s.focus)c.circle(t.x,t.y,t.diameter/2+(lunaros::graphics::metrics::native?4:2),white,true);
      c.circle(t.x,t.y,t.diameter/2,s.apps[i].color);
      if(s.apps[i].icon_resource&&*s.apps[i].icon_resource){
        const int d=static_cast<int>(t.diameter*.58f);c.glyph_box(lunaros::icons::find(s.apps[i].icon_resource),x-d/2,y-d/2,d,d,white,true,static_cast<uint8_t>(t.detail*255+.5f));
      }else c.icon(s.apps[i].icon,x,y,static_cast<int>(t.diameter*.58f),static_cast<uint8_t>(t.detail*255+.5f));
      if(s.has_badge(i)&&t.detail>0){c.circle(x+static_cast<int>(r*.73f),y-static_cast<int>(r*.73f),5*t.detail,0);c.circle(x+static_cast<int>(r*.73f),y-static_cast<int>(r*.73f),4*t.detail,theme::danger);}
      if(s.preferences.show_labels&&s.preferences.layout!=DesktopLayout::Honeycomb) {
        const bool dense=s.preferences.layout==DesktopLayout::Grid&&(s.grid_columns>2||s.grid_rows>2);
        namespace m=lunaros::graphics::metrics;
        c.centered(x,y+r+(m::native?m::gap:(dense?4:10)),s.apps[i].name,m::scaled_text_size(m::native?typography::secondary.size:dense?12:typography::label.size,s.preferences.text_size));
      }
    }
  }
  if(!lunaros::graphics::metrics::native&&s.compact.enabled&&s.surface==DesktopSurface::Launcher&&(s.compact.mode==CompactMode::Desktop||s.compact.mode==CompactMode::Edit))paint_compact(c,s);
  context.profile.mark(3);
  raster.clip({0,0,canvas_layout::side,canvas_layout::side});
  const bool automatic=!(lunaros::graphics::metrics::native&&s.home.enabled&&s.surface==DesktopSurface::Launcher)&&s.preferences.auto_hide_status;
  return build_status_overlay(raster,s.title(),status,s.status_navigation,automatic)&&raster.good();
  }))return false;
  // Shared in-place rotation avoids an extra full framebuffer and retains the
  // transfer queue's exclusive ownership of this canvas.
  display::rotate_rgb565(pixels,s.preferences.rotation,canvas_layout::side);
  if(indicator){
    if(!draw_edge_back_indicator(s.edge_navigation,pixels,s.preferences.rotation,s.preferences.antialias,renderer))return false;
    if(!draw_home_indicator(s.home_navigation,pixels,s.preferences.rotation,s.preferences.antialias,renderer))return false;
  }
  profile.mark(4);
#ifdef ESP_PLATFORM
  if(const auto sample=profile.finish();sample.calls)ESP_LOGI("desktop.render",
    "LAUNCHER_PROFILE calls=%u window_us=%llu worker_task_us=%u background_us=%llu setup_us=%llu lines_us=%llu icons_us=%llu chrome_us=%llu",
    unsigned(sample.calls),(unsigned long long)sample.window_us,unsigned(sample.worker_task_us),
    (unsigned long long)sample.wall[0],(unsigned long long)sample.wall[1],(unsigned long long)sample.wall[2],
    (unsigned long long)sample.wall[3],(unsigned long long)sample.wall[4]);
#else
  (void)profile.finish();
#endif
  return true;
}
} // namespace lunaredu::desktop
