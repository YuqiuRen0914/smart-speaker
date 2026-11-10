#include "desktop/navigation/snapshot.h"
#include "lunaros_ui_metrics.h"
#include "shell/widget_ids.h"
#include "desktop/navigation/home_navigation_renderer.h"
#include "desktop/graphics/icon_resource.h"
#include "desktop/ui/render_frame.h"
#include "desktop/navigation/compositor.h"
#include "desktop/graphics/canvas_spec.h"
#include "desktop/ui/status_bar.h"
#include "desktop/ui/system_view_painter.h"
#include "desktop/graphics/desktop.h"
#include "graphics/color.h"
#include "desktop/graphics/page.h"
#include "desktop/graphics/scene_icon.h"
#include "navigation/direction_layout.h"
#include "desktop/ui/status_bar.h"
#include "navigation/edge_navigation_renderer.h"
#include "display/pipeline/pixel_orientation.h"
#include "graphics/icon_draw.h"
#include "desktop/ui/internal/chrome_coverage.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <memory>
namespace lunaredu::lunaros::graphics {
namespace ui=::lunaredu::lunaros::graphics::metrics;
Page select_desktop(SceneRuntime &runtime,uint64_t key,const desktop::DesktopShell &shell) {
  return runtime.select(key,shell.surface!=desktop::DesktopSurface::Launcher,desktop::shell_content_revision(shell));
}
bool render_desktop(SceneRuntime &runtime,Page page,const desktop::DesktopShell &shell,const desktop::DesktopStatus &status,
    RasterOutput output,desktop::DesktopPainter painter,bool indicator,uint16_t *source) {
  struct Context { const desktop::DesktopShell &shell; const desktop::DesktopStatus &status; RasterOutput output; desktop::DesktopPainter painter; bool indicator; uint16_t *source; } context{shell,status,output,painter,indicator,source};
  return runtime.render_with(page,{&context,[](void *p,Scene &scene,uint64_t epoch,Scene::Emit emit,void *target) {
    const auto &c=*static_cast<Context *>(p);
    return paint_desktop(scene,epoch,c.shell,c.status,c.output,c.painter,emit,target,c.indicator,c.source);
  }});
}
namespace {
using namespace desktop;
constexpr uint16_t white=theme::text,muted=theme::appsecondary,panel=theme::appsurface;
using namespace desktop::shell_widgets;
bool switch_setting(DesktopPreferenceAction setting){
  return setting==DesktopPreferenceAction::Labels||setting==DesktopPreferenceAction::Antialias||setting==DesktopPreferenceAction::Animations||setting==DesktopPreferenceAction::StatusBar||setting==DesktopPreferenceAction::HoneycombLines;
}
void menus(Scene &scene,const DesktopShell &s){
  const ContentLayout layout(s.preferences.auto_hide_status);
  if(s.surface==DesktopSurface::Actions&&s.active_menu){
    const auto &menu=*s.active_menu;
    if(menu.layout==DesktopMenuLayout::Direction){
      for(size_t i=0;i<menu.count;++i){const size_t edge=(i+s.preferences.rotation)%4;const int y=i==0?layout.bottom-direction_layout::height:i==2?layout.top:layout.center_y()-direction_layout::height/2;scene.button(row_id+edge,{direction_layout::x(i),y,ui::px(56),ui::px(38)},"",desktop::typography::label.size,panel,white,s.menu_selected(menu.items[edge]));
        const lunaros::icons::Icon arrows[]={lunaros::icons::Icon::ArrowDown,lunaros::icons::Icon::ArrowLeft,lunaros::icons::Icon::ArrowUp,lunaros::icons::Icon::ArrowRight};
        desktop::scene_icon(scene,6000+edge,{ui::px(14),ui::px(5),ui::px(28),ui::px(28)},arrows[i],white,row_id+edge,s.preferences.antialias);}
      scene.label(40,{ui::px(68),layout.center_y()-ui::px(19),ui::px(104),ui::px(38)},"选择底边",desktop::typography::label.size,muted);return;
    }
    if(menu.layout==DesktopMenuLayout::Grid2x2){
      for(size_t page_index=0;page_index<(menu.count+3)/4;++page_index){
        int offset=0;
        if(s.moving||s.page_drag){
          if(page_index==s.source_page)offset=int(s.page_offset);
          else if(page_index==s.destination_page)offset=int(s.page_offset)+s.page_direction*desktop::canvas_layout::side;
          else continue;
        }else if(page_index!=s.row/4)continue;
        const auto group=uint32_t(5100+page_index);
        const int height=(layout.height()-layout.gap-(menu.count>4?ui::line_height(12,16):0))/2;
        scene.grid(group,{layout.inset+offset,layout.top,layout.width,height*2+layout.gap},2,2,layout.gap,layout.gap);
        for(size_t i=page_index*4;i<std::min(menu.count,(page_index+1)*4);++i){
          const auto &item=menu.items[i];
          const auto fill=lunaros::raster::blend_rgb565(panel,item.color,24);
          scene.button(row_id+i,{0,0,(layout.width-layout.gap)/2,height},"",ui::font(16),fill,white,s.menu_selected(item)||(s.keys_enabled&&s.row==i),!s.moving&&!s.page_drag,group);
          scene.grid_item(row_id+i,unsigned(i%2),unsigned((i%4)/2));
          scene.flex_layout(row_id+i,false,ui::px(4),ui::px(12),Align::Center);
          if(item.glyph)scene.image(6000+i*3,{0,0,ui::px(32),ui::px(32)},{38,38,38,nullptr,item.glyph,item.danger?theme::danger:item.color},row_id+i);
          else page::icon_badge(scene,6000+i*3,{0,0,ui::px(32),ui::px(32)},desktop::desktop_icon(item.icon),item.danger?theme::danger:item.color,row_id+i,s.preferences.antialias);
          scene.flow_item(6000+i*3,0,1); // The hidden native Button label is child zero.
          scene.label(100+i,{0,0,ui::px(84),ui::px(26)},item.label,ui::font(20),item.danger?theme::danger:white,Align::Left,row_id+i,true);
          scene.flow_item(100+i,0,2);
          if(item.caption){scene.label(200+i,{0,0,ui::px(88),ui::px(16)},item.caption,ui::font(12),muted,Align::Left,row_id+i,true);scene.flow_item(200+i,0,3);}
        }
      }
      if(menu.count>4){char text[24];std::snprintf(text,sizeof(text),"%u / %u",unsigned(s.row/4+1),unsigned((menu.count+3)/4));scene.label(40,{ui::px(8),ui::px(216),ui::px(224),ui::px(16)},text,ui::font(12),muted);}return;
    }
    scene.container(list_id,{ui::px(8),layout.top,ui::px(224),layout.height()},s.list_offset*layout.row_pitch(),scroll_id);
    scene.flex_layout(list_id,false,layout.gap);
    for(size_t i=0;i<menu.count;++i){
      const auto &item=menu.items[i];
      if(switch_setting(item.preference)||item.toggle){
        page::switch_row(scene,row_id+i,toggle_id+i,{0,0,ui::px(224),layout.row_height()},item.label,s.menu_selected(item),s.keys_enabled&&s.row==i,true,list_id,48);
      }else{
        const char *value=s.menu_value(item.id);
        const auto *identity=s.menu_identity(item.id);
        page::row(scene,row_id+i,{0,0,ui::px(224),layout.row_height()},identity&&*identity->title?identity->title:item.label,value&&*value?value:"",
                  s.keys_enabled&&s.row==i,true,list_id,item.danger?theme::Tone::Danger:theme::Tone::Normal,48);
      }
      scene.flow_item(row_id+i,0,unsigned(i));
      const auto *identity=s.menu_identity(item.id);
      if(identity&&identity->image){
        const auto &image=*identity->image;
        scene.image(6200+i*3,{ui::px(8),(layout.row_height()-ui::px(32))/2,ui::px(32),ui::px(32)},{image.width,image.height,image.width,image.pixels.data(),nullptr,white,true},row_id+i);
        scene.overlay(6200+i*3,{ui::px(8),(layout.row_height()-ui::px(32))/2,ui::px(32),ui::px(32)});
      }else page::icon_badge(scene,6000+i*3,{ui::px(8),(layout.row_height()-ui::px(30))/2,ui::px(30),ui::px(30)},desktop::desktop_icon(item.icon),item.danger?theme::danger:theme::appaccent,row_id+i,s.preferences.antialias);
    }return;
  }
  scene.container(list_id,{ui::px(8),layout.top,ui::px(224),layout.height()},s.list_offset*layout.row_pitch(),scroll_id);
  scene.flex_layout(list_id,false,layout.gap);
  if(s.surface==DesktopSurface::System){
    DesktopSurface surfaces[static_cast<size_t>(DesktopSurface::Count)]{};const auto count=system_surfaces(s,surfaces,static_cast<size_t>(DesktopSurface::Count));
    for(size_t i=0;i<count;++i){const auto &e=desktop_surface_extension(s,surfaces[i]);const char *title=e.title?e.title(s):e.application?e.application->name:"系统";scene.button(row_id+i,{0,0,ui::px(224),layout.row_height()},title,ui::font(16),panel,white,s.keys_enabled&&s.row==i,true,list_id);scene.flow_item(row_id+i,0,unsigned(i));}
  }

}


}
static bool build_desktop(Scene &scene,const desktop::DesktopShell &s,const desktop::DesktopStatus &status){
  using namespace desktop;
  if(s.surface==DesktopSurface::Launcher)return false;
  if(const auto build=desktop_surface_extension(s,s.surface).build)build(scene,s,status);
  else menus(scene,s);
  return true;
}
static void desktop_assets(const desktop::DesktopShell &s,uint16_t *pixels){
  if(const auto paint=desktop::desktop_surface_extension(s,s.surface).assets)paint(s,pixels);
}
bool paint_desktop(Scene &scene,uint64_t epoch,const desktop::DesktopShell &s,const desktop::DesktopStatus &status,RasterOutput output,desktop::DesktopPainter painter,Scene::Emit emit,void *context,bool indicator,uint16_t *source){
  auto *pixels=output.pixels;
  if(!pixels)return false;
  scene.text_scale(ui::text_scale_percent(s.preferences.text_size));
  if(indicator&&source&&s.home_navigation.progress>0){
    desktop::NavigationSnapshot snapshot(s);
    if(!snapshot)return false;
    snapshot->surface=s.navigation_surface;snapshot->home_navigation.reset();
    if(!paint_desktop(scene,epoch,*snapshot,status,{source,desktop::kDesktopCanvas.pixels()},painter,emit,context,false))return false;
    snapshot->surface=desktop::DesktopSurface::Launcher;snapshot->settle_motion();
    if(!desktop::draw_launcher(*snapshot,status,pixels,painter,false,nullptr,&scene))return false;
    const auto tile=snapshot->tile(snapshot->focus);
    if(!desktop::compose_home_card(pixels,source,s.home_navigation,{tile.x,tile.y,tile.diameter},s.preferences.rotation,s.preferences.antialias,&scene)||
       !desktop::draw_home_indicator(s.home_navigation,pixels,s.preferences.rotation,s.preferences.antialias,&scene))return false;
    if(output.coherence)output.coherence->invalidate();
    if(output.damage){*output.damage=::lunaredu::display::FrameDamageMap(desktop::kDesktopCanvas.width,desktop::kDesktopCanvas.height);output.damage->mark({0,0,desktop::kDesktopCanvas.width,desktop::kDesktopCanvas.height});}
    return true;
  }
  if(s.surface==desktop::DesktopSurface::Launcher){scene.clear();if(!desktop::draw_launcher(s,status,pixels,painter,indicator,nullptr,&scene))return false;if(output.coherence)output.coherence->invalidate();if(output.damage){*output.damage=::lunaredu::display::FrameDamageMap(desktop::kDesktopCanvas.width,desktop::kDesktopCanvas.height);output.damage->mark({0,0,desktop::kDesktopCanvas.width,desktop::kDesktopCanvas.height});}return true;}
  const bool raw_assets=desktop::desktop_surface_extension(s,s.surface).assets!=nullptr;
  if(!scene.begin(epoch,theme::appbackground,s.preferences.antialias,emit,context)||!build_desktop(scene,s,status)||
     (!raw_assets&&!desktop::build_status_overlay(scene,s.title(),status,s.status_navigation,s.preferences.auto_hide_status,painter,s.preferences.antialias))||!scene.render(output))return false;
  desktop_assets(s,pixels);
  if(raw_assets&&!desktop::paint_status_overlay(scene,desktop::kDesktopCanvas,pixels,s.title(),status,s.status_navigation,s.preferences.auto_hide_status,s.preferences.antialias))return false;
  ::lunaredu::display::rotate_rgb565(pixels,s.preferences.rotation,desktop::kDesktopCanvas.width);
  if(indicator){if(!desktop::draw_edge_back_indicator(s.edge_navigation,pixels,s.preferences.rotation,s.preferences.antialias,&scene)||!desktop::draw_home_indicator(s.home_navigation,pixels,s.preferences.rotation,s.preferences.antialias,&scene))return false;}
  const bool unknown=desktop::desktop_surface_extension(s,s.surface).assets||(s.preferences.rotation&3)||(indicator&&source&&s.home_navigation.progress>0);
  if(unknown){if(output.coherence)output.coherence->invalidate();if(output.damage)output.damage->mark({0,0,desktop::kDesktopCanvas.width,desktop::kDesktopCanvas.height});}
  else {const auto chrome=desktop::ChromeCoverage::capture(s,status,true,0);if(output.coherence)chrome.mark(output.coherence->overwritten);if(output.damage)chrome.mark(*output.damage);}
  return true;
}
}

namespace lunaredu::desktop {
void ShellPresentation::paint(UiPaintPort &port, bool) const {
    if (!unavailable_title.empty()) {
      paint_unavailable(unavailable_title.c_str(), unavailable_reason.c_str(), port); return;
    }
  }
}
