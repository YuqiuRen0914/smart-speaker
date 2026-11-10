#include "lunaros_ui_metrics.h"
#include "lunaros_settings_renderer.h"
#include "lunaros_ui_theme.h"
#include "desktop/graphics/page.h"
#include "desktop/ui/content_layout.h"
#include "desktop/graphics/scene_icon.h"
#include "desktop/graphics/icon_resource.h"
#include <cstdio>
#include <cstring>
#include <string>
namespace lunaredu::settings {
namespace ui=::lunaredu::lunaros::graphics::metrics;
using namespace desktop;
using namespace lunaros::graphics;
namespace {
bool application_icon(Scene &scene,uint32_t id,int y,const DesktopApplication *app,uint32_t parent,bool aa){
  constexpr int side=28;
  const uint16_t color=app?app->color:theme::appsecondary;
  const auto icon=app?(app->icon_resource&&*app->icon_resource?lunaros::icons::find(app->icon_resource):desktop_icon(app->icon)):lunaros::icons::Icon::Display;
  if constexpr(ui::native){
    const Rect box{ui::inset,y,ui::row_icon,ui::row_icon};
    return desktop::scene_icon(scene,id,box,icon,color,parent,aa)&&scene.overlay(id,box);
  }
  const Rect box{ui::px(8),y,side,side};
  return scene.panel(id,box,color,parent,side/2)&&scene.overlay(id,box)&&
    desktop::scene_icon(scene,id+0x10000,{6,6,16,16},icon,0xffff,id,aa);
}
}
bool build_preferences_surface(Scene &scene,const DesktopShell &s){
 if(s.surface!=DesktopSurface::Startup && s.surface!=DesktopSurface::Settings)return false;
 const ContentLayout layout(s.preferences.auto_hide_status);
 constexpr uint32_t row_id=1000,toggle_id=2000,list_id=5000,scroll_id=5001;
 scene.container(list_id,{ui::px(8),layout.top,ui::px(224),layout.height()},s.list_offset*layout.row_pitch(),scroll_id);
 scene.flex_layout(list_id,false,layout.row_pitch()-layout.row_height());
  if(s.surface==DesktopSurface::Startup){
    for(size_t i=0;i<=s.count;++i){
      page::row(scene,row_id+i,{0,0,ui::px(224),layout.row_height()},i?s.apps[i-1].name:"桌面","",i? !std::strcmp(s.preferences.startup_id,s.apps[i-1].id):!*s.preferences.startup_id,true,list_id,theme::Tone::Normal,44);
      scene.flow_item(row_id+i,0,i);
      // Keep a row of overscan for touch dragging; the maximum 33-entry catalog
      // must not retain a color texture for every offscreen application.
      if(int(i)+1>=s.list_offset&&int(i)<=s.list_offset+layout.rows())
        application_icon(scene,6000+i,(layout.row_height()-(ui::native?ui::row_icon:28))/2,i?&s.apps[i-1]:nullptr,row_id+i,s.preferences.antialias);
    }
  }else if(s.surface==DesktopSurface::Settings){
    const char *layouts[]={"网格","单图标","蜂窝"};
    const char *layout_name=layouts[unsigned(s.preferences.layout)];
    const char *titles[]={"桌面形式","应用名称","屏幕方向","抗锯齿","界面动效","开机应用","系统顶栏","文字大小"};
    const char *values[]={layout_name,"",preference_value(s,"os.rotation"),"","",preference_value(s,"os.startup"),preference_value(s,"os.statusbar"),preference_value(s,"os.text_size")};
    const lunaros::icons::Icon icons[]={lunaros::icons::Icon::Games,lunaros::icons::Icon::Code,lunaros::icons::Icon::Axes,lunaros::icons::Icon::Adjust,lunaros::icons::Icon::Motion,lunaros::icons::Icon::Remote,lunaros::icons::Icon::Display,lunaros::icons::Icon::Code};
    for(size_t i=0;i<(ui::native?8:5);++i){
      const size_t setting=i+(ui::native?0:2);
      if(setting==1||setting==3||setting==4||setting==6){page::switch_row(scene,row_id+i,toggle_id+i,{0,0,ui::px(224),layout.row_height()},titles[setting],setting==1?s.preferences.show_labels:setting==3?s.preferences.antialias:setting==4?s.preferences.animations:!s.preferences.auto_hide_status,s.keys_enabled&&s.row==i,true,list_id,48);}
      else page::row(scene,row_id+i,{0,0,ui::px(224),layout.row_height()},titles[setting],values[setting],s.keys_enabled&&s.row==i,true,list_id,theme::Tone::Normal,48);
      scene.flow_item(row_id+i,0,i);
      page::icon_badge(scene,6000+i*2,{ui::px(8),(layout.row_height()-ui::px(30))/2,ui::px(30),ui::px(30)},icons[setting],setting==4?theme::violet:theme::appaccent,row_id+i,s.preferences.antialias);
    }
  }return true;
}
}
