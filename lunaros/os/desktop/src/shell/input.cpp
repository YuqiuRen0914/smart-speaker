#include "desktop/navigation/runtime.h"
#include "desktop/graphics/canvas_spec.h"
#include "graphics/compiled_layout.h"
#include "internal.h"

namespace lunaredu::desktop {

using namespace shell_detail;

void DesktopShell::cancel_contact() {
  if(lunaros::graphics::metrics::native&&home.enabled)home.cancel();
  if(!lunaros::graphics::metrics::native&&compact.enabled)compact.cancel();
  if(status_navigation.contact)status_navigation.cancel();
  edge_navigation.reset(touch_enabled&&surface!=DesktopSurface::Launcher&&!paged_menu());
  if(page_drag){page_drag=false;page_offset=0;moving=false;}
  blocked_contact=true; contact=false; dragged=false;
}

bool DesktopShell::command(DesktopCommand cmd,bool system_source) {
  status_navigation.activity(now_ms);
  if(status_navigation.contact)status_navigation.cancel();
  if (!count || (!keys_enabled && !system_source)) return false;
  if(home_navigation.contact||home_navigation.moving)home_navigation.cancel(now_ms,preferences.animations);
  if(lunaros::graphics::metrics::native&&home.enabled&&surface==DesktopSurface::Launcher&&home.command(*this,unsigned(cmd)))return true;
  if(!lunaros::graphics::metrics::native&&compact.enabled&&surface==DesktopSurface::Launcher&&compact.command(*this,unsigned(cmd)))return true;
  if(cmd>=DesktopCommand::Left && cmd<=DesktopCommand::Down) {
    const int physical=cmd==DesktopCommand::Right?0:cmd==DesktopCommand::Down?1:cmd==DesktopCommand::Left?2:3;
    constexpr DesktopCommand directions[]={DesktopCommand::Right,DesktopCommand::Down,DesktopCommand::Left,DesktopCommand::Up};
    cmd=directions[(physical+4-preferences.rotation)%4];
  }
  const bool touch_drag=page_drag;
  cancel_contact();
  if(cmd==DesktopCommand::Home||cmd==DesktopCommand::System)
    if(const auto close=desktop_surface_extension(*this,surface).close)close(*this);
  if(cmd!=DesktopCommand::Home && cmd!=DesktopCommand::System && desktop_surface_extension(*this,surface).command && desktop_surface_extension(*this,surface).command(*this,cmd))return true;
  if(cmd==DesktopCommand::Back&&surface==DesktopSurface::Actions&&active_menu){
    for(size_t i=0;i<menu_count;++i)for(size_t n=0;n<menus[i].count;++n)
      if(menus[i].items[n].submenu&&!std::strcmp(menus[i].items[n].id,active_menu->id)){
        if(!open_menu(menus[i].id))return false;
        row=static_cast<uint8_t>(n);list_offset=row>=ContentLayout(preferences.auto_hide_status).rows()?row-ContentLayout(preferences.auto_hide_status).rows()+1:0;return true;
      }
  }
  if (cmd==DesktopCommand::Home || cmd==DesktopCommand::Back) { leaf_menu=nullptr;home_navigation.reset();settle_motion();surface=DesktopSurface::Launcher;row=list_offset=0;return true; }
  if (cmd==DesktopCommand::System) { leaf_menu=nullptr;home_navigation.reset(touch_enabled);settle_motion();surface=DesktopSurface::System;row=list_offset=0;return true; }
  if (surface!=DesktopSurface::Launcher) {
    if (cmd==DesktopCommand::Confirm) {if(!touch_drag){moving=false;page_offset=0;choose_row(*this);}}
    else if (surface==DesktopSurface::System || surface==DesktopSurface::Actions || desktop_surface_extension(*this,surface).rows) {
      const auto row_count=desktop_surface_extension(*this,surface).rows;
      const int rows=surface==DesktopSurface::Actions&&active_menu?static_cast<int>(active_menu->count):row_count?static_cast<int>(row_count(*this)):static_cast<int>(system_surfaces(*this,nullptr,0));
      if(!rows)return false;
      if(surface==DesktopSurface::Actions&&active_menu&&active_menu->layout==DesktopMenuLayout::Direction){
        if(cmd==DesktopCommand::Down)row=preferences.rotation;else if(cmd==DesktopCommand::Left)row=(1+preferences.rotation)%4;
        else if(cmd==DesktopCommand::Up)row=(2+preferences.rotation)%4;else if(cmd==DesktopCommand::Right)row=(3+preferences.rotation)%4;
        else row=static_cast<uint8_t>((row+4+(cmd==DesktopCommand::Previous?-1:1))%4);
        return true;
      }
      if(grid_menu()){
        const int page=row/4,cell=row%4,last_page=(rows-1)/4;
        int candidate=row;
        if(cmd==DesktopCommand::Next||cmd==DesktopCommand::Previous)candidate=(row+rows+(cmd==DesktopCommand::Next?1:-1))%rows;
        else if(cmd==DesktopCommand::Right)candidate=cell%2==0?std::min(row+1,rows-1):page<last_page?std::min((page+1)*4+(cell/2)*2,rows-1):row;
        else if(cmd==DesktopCommand::Left)candidate=cell%2?row-1:page? (page-1)*4+(cell/2)*2+1:row;
        else if(cmd==DesktopCommand::Down&&cell<2&&row+2<rows)candidate=row+2;
        else if(cmd==DesktopCommand::Up&&cell>=2)candidate=row-2;
        navigate_menu(candidate);return true;
      }
      const int step=cmd==DesktopCommand::Previous||cmd==DesktopCommand::Up||cmd==DesktopCommand::Left?-1:1;
      row=static_cast<uint8_t>((row+rows+step)%rows);
      if(row<list_offset)list_offset=row;
      const int visible=ContentLayout(preferences.auto_hide_status).rows();
      if(row>=list_offset+visible)list_offset=row-visible+1;
    }
    return true;
  }
  if (cmd==DesktopCommand::Confirm) { if(!touch_drag&&launcher_visible(focus)){settle_motion();enter(*this,focus);}return true; }
  if (cmd==DesktopCommand::Previous || cmd==DesktopCommand::Next) { const int step=cmd==DesktopCommand::Previous?-1:1;size_t next=target_focus;for(size_t n=0;n<count;++n){next=(next+count+step)%count;if(launcher_visible(next))break;}navigate(next,step);return true; }
  // Direction commands describe physical keys and are rotated with the screen.
  int direction=cmd==DesktopCommand::Right?0:cmd==DesktopCommand::Down?1:cmd==DesktopCommand::Left?2:3;
  if (preferences.layout!=DesktopLayout::Honeycomb) {
    const int step=direction==0?1:direction==2?-1:preferences.layout==DesktopLayout::Grid?(direction==1?grid_columns:-int(grid_columns)):(direction==1?1:-1);
    navigate((static_cast<int>(target_focus)+static_cast<int>(count)+step)%static_cast<int>(count),step);return true;
  }
  const float vx=direction==0?1:direction==2?-1:0,vy=direction==1?1:direction==3?-1:0;
  auto from=point(target_focus); size_t best=target_focus;float score=1e9f;
  for(size_t i=0;i<count;++i) { if(!launcher_visible(i))continue;auto p=point(i);float x=p.x-from.x,y=p.y-from.y,along=x*vx+y*vy,lateral=std::abs(x*vy-y*vx);
    if (along<=1 || lateral>along*1.8f) continue;
    const float candidate=std::hypot(x,y)+1.5f*lateral;
    if (candidate<score) {score=candidate;best=i;}
  }
  navigate(best);return true;
}
bool DesktopShell::activate_row(size_t item) {
  if(surface==DesktopSurface::Launcher||desktop_surface_extension(*this,surface).row_activation==DesktopRowActivation::Disabled||moving||page_drag)return false;
  const size_t limit=surface==DesktopSurface::Actions&&active_menu?active_menu->count:
    desktop_surface_extension(*this,surface).rows?desktop_surface_extension(*this,surface).rows(*this):surface==DesktopSurface::System?system_surfaces(*this,nullptr,0):0;
  if(item>=limit)return false;
  row=static_cast<uint8_t>(item);choose_row(*this);return true;
}
bool DesktopShell::pointer(bool pressed,bool valid,int x,int y,bool route_status) {
  const auto top_point=pressed?logical(x,y):last;
  const bool was_showing=status_navigation.showing(preferences.auto_hide_status);
  if(route_status&&status_navigation.pointer(pressed,valid&&touch_enabled,top_point.x,top_point.y,now_ms,preferences.auto_hide_status)){
    contact=false;dragged=false;blocked_contact=pressed;
    if(status_navigation.take_system())return command(DesktopCommand::System,true);
    return was_showing!=status_navigation.showing(preferences.auto_hide_status);
  }
  const bool home_enabled=touch_enabled&&surface!=DesktopSurface::Launcher;
  if(home_enabled!=home_navigation.enabled&&!home_navigation.exiting())home_navigation.reset(home_enabled);
  const auto home_point=pressed?logical(x,y):last;
  if(home_navigation.pointer(pressed,valid,home_point.x,home_point.y,now_ms,preferences.animations)){
    edge_navigation.reset(home_enabled&&!paged_menu());
    navigation_surface=surface;contact=false;dragged=false;
    (void)home_navigation.take_request();return true;
  }
  const bool edge_enabled=home_enabled&&!paged_menu();
  if(edge_navigation.enabled!=edge_enabled)edge_navigation.reset(edge_enabled);
  if(edge_navigation.pointer(pressed,valid,home_point.x,home_point.y,now_ms,preferences.animations)){
    // Lock out the row gesture that began before horizontal direction won.
    contact=false;blocked_contact=true;dragged=false;return true;
  }
  if (!touch_enabled || !valid || (pressed && (x<0||x>=canvas_layout::side||y<0||y>=canvas_layout::side))) { const bool changed=page_drag||(!lunaros::graphics::metrics::native&&compact.enabled&&(compact.contact||compact.dragging));cancel_contact();return changed; }
  if (blocked_contact) {if (!pressed) blocked_contact=false;return false;}
  if(lunaros::graphics::metrics::native&&home.enabled&&surface==DesktopSurface::Launcher){const auto p=pressed?logical(x,y):DesktopPoint{float(home.last_x),float(home.last_y)};return home.pointer(*this,pressed,valid,int(p.x),int(p.y));}
  if(!lunaros::graphics::metrics::native&&compact.enabled&&surface==DesktopSurface::Launcher){const auto p=pressed?logical(x,y):DesktopPoint{float(compact.last_x),float(compact.last_y)};if(compact.pointer(*this,pressed,valid,int(p.x),int(p.y)))return true;}
  auto p=pressed?logical(x,y):last; // Controllers may clear coordinates on release.
  if (pressed && !contact) {
    if(moving&&(grid_menu()||preferences.layout!=DesktopLayout::Honeycomb)){cancel_contact();return false;}
    moving=false;target_focus=focus;contact=true;dragged=false;start=last=p;camera_start=camera;contact_started_ms=now_ms;return false;
  }
  if (!contact) return false;
  const float dx=p.x-start.x,dy=p.y-start.y;
  if (pressed) {
    last=p;dragged=dragged||std::hypot(dx,dy)>=(lunaros::graphics::metrics::native?lunaros::graphics::metrics::gesture_slop:8);
    if(dragged&&paged_menu()&&start.y>=launcher_top()&&preferences.animations&&clock_ready&&std::abs(dx)>std::abs(dy)){
      source_page=row/4;const size_t pages=(active_menu->count+3)/4;
      destination_page=dx<0?std::min(pages-1,source_page+1):(source_page?source_page-1:0);
      offset_target=dx<0?-float(canvas_layout::side):float(canvas_layout::side);page_direction=dx<0?1:-1;
      page_offset=std::clamp(dx,-float(canvas_layout::side),float(canvas_layout::side))*(destination_page==source_page?.2f:1.f);
      page_drag=true;return true;
    }
    if (dragged && surface==DesktopSurface::Launcher && preferences.layout==DesktopLayout::Honeycomb && start.y>=launcher_top()) {
      return pan_honeycomb(camera_start,dx,dy);
    }
    if(dragged&&surface==DesktopSurface::Launcher&&preferences.layout!=DesktopLayout::Honeycomb&&start.y>=launcher_top()&&preferences.animations&&clock_ready&&std::abs(dx)>std::abs(dy)){
      const size_t size=launcher_page_size();
      source_page=focus/size;const size_t pages=(count+size-1)/size;
      destination_page=dx<0?std::min(pages-1,source_page+1):(source_page?source_page-1:0);
      offset_target=dx<0?-float(canvas_layout::side):float(canvas_layout::side);
      page_direction=dx<0?1:-1;
      page_offset=std::clamp(dx,-float(canvas_layout::side),float(canvas_layout::side))*(destination_page==source_page?.2f:1.f);
      page_drag=true;return true;
    }
    return false;
  }
  contact=false;
  if (dragged) {
    if(surface==DesktopSurface::Launcher&&preferences.layout==DesktopLayout::Honeycomb){navigate(focus);}
    else if(page_drag){
      const size_t size=grid_menu()?4:launcher_page_size();
      const bool commit=std::abs(dx)>=(lunaros::graphics::metrics::native?lunaros::graphics::metrics::gesture_travel:32)&&destination_page!=source_page;
      offset_from=page_offset;motion_started=now_ms;moving=true;page_drag=false;
      if(commit){if(grid_menu())row=static_cast<uint8_t>(std::min(active_menu->count-1,destination_page*4+row%4));else focus=target_focus=std::min(count-1,destination_page*size);}
      else offset_target=0;
    }else if(paged_menu()&&std::abs(dx)>=(lunaros::graphics::metrics::native?lunaros::graphics::metrics::gesture_travel:32)&&std::abs(dx)>std::abs(dy)){
      const size_t page=row/4,pages=(active_menu->count+3)/4;
      const size_t next=dx<0?std::min(pages-1,page+1):(page?page-1:0);
      navigate_menu(std::min(active_menu->count-1,next*4+row%4));
    }
    else if (surface==DesktopSurface::Launcher && preferences.layout!=DesktopLayout::Honeycomb && std::abs(dx)>=(lunaros::graphics::metrics::native?lunaros::graphics::metrics::gesture_travel:32) && std::abs(dx)>std::abs(dy)) {
      const int step=int(launcher_page_size());
      if (dx<0) navigate(std::min(count-1,(focus/step+1)*step),1); else navigate(focus>=static_cast<size_t>(step)?(focus/step-1)*step:0,-1);
    }
    return true;
  }
  if(surface==DesktopSurface::Launcher) {
    for(size_t i=0;i<count;++i) {
      auto t=tile(i);bool hit=distance(p,{t.x,t.y})<=std::max(22.f,t.diameter/2);
      if(preferences.layout==DesktopLayout::Grid) {
        if constexpr(lunaros::graphics::metrics::native) {
          const auto &g=compiled_layout::grid(grid_columns,grid_rows,preferences.auto_hide_status,preferences.show_labels);
          const auto r=(i%launcher_page_size())/grid_columns;
          hit=p.x>=t.x-g.width/2&&p.x<t.x+g.width/2&&p.y>=g.top+int(r)*g.height&&p.y<g.top+int(r+1)*g.height;
        } else {
        const float width=float(canvas_layout::side)/grid_columns,height=float(canvas_layout::side-launcher_top())/grid_rows;
        const size_t row=(i%launcher_page_size())/grid_columns;
        hit=p.x>=t.x-width/2&&p.x<t.x+width/2&&p.y>=launcher_top()+height*row&&p.y<launcher_top()+height*(row+1);
        if(grid_columns==2&&grid_rows==2)hit=p.x>=t.x-56&&p.x<t.x+56&&p.y>=launcher_top()+height*row&&p.y<launcher_top()+height*(row+1);
        }
      }
      if(preferences.layout==DesktopLayout::Single)hit=p.y>=launcher_top()&&p.y<canvas_layout::side;
      if(t.clickable && hit){enter(*this,i);return true;}
    }
  }
  return false;
}

int DesktopShell::take_launch() {int value=launch;launch=-1;return value;}

} // namespace lunaredu::desktop
