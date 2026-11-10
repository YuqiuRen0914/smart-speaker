#include "desktop/graphics/canvas_spec.h"
#include "graphics/compiled_layout.h"
#include "graphics/compact_honeycomb.h"
#include "internal.h"
#include "input/navigation_repeat.h"
#include "graphics/animation.h"

namespace lunaredu::desktop {

using namespace shell_detail;
static_assert(DesktopShell::capacity==compact_honeycomb::capacity);

DesktopPoint DesktopShell::point(size_t index) const {
  if constexpr(!lunaros::graphics::metrics::native){
    if(index>=count||honeycomb_slots[index]>=compact_honeycomb::capacity)return {};
    const auto p=compact_honeycomb::center(compact_honeycomb::cells[static_cast<unsigned>(apps[index].category)][honeycomb_slots[index]]);
    return {p.u*compact_honeycomb::x_unit,p.v*compact_honeycomb::y_unit};
  }
  if(index>=compiled_layout::honeycomb_points.size())return {};
  const auto p=compiled_layout::honeycomb_points[index];return {p.x,p.y};
}
void DesktopShell::select(size_t app) {
  reset_joystick();
  if(count){app=std::min(app,count-1);if(!launcher_visible(app)){app=0;while(app<count&&!launcher_visible(app))++app;if(app==count)app=0;}focus=target_focus=app;camera=camera_target=point(focus);}
  moving=page_drag=false; page_offset=0;
}
void DesktopShell::settle_motion() { select(target_focus); }

void DesktopShell::reset_joystick() {
  joystick_panning=joystick_armed=false;joystick_velocity={};
}
bool DesktopShell::joystick(int x,int y,uint32_t now) {
  if(!lunaros::graphics::metrics::native&&compact.enabled&&compact.mode!=CompactMode::Desktop&&compact.mode!=CompactMode::Edit){reset_joystick();return false;}
  if(!count||surface!=DesktopSurface::Launcher||preferences.layout!=DesktopLayout::Honeycomb){reset_joystick();return false;}
  const auto axis=lunaros::input::rotate_navigation({x,y},preferences.rotation);
  const int64_t squared=int64_t(axis.x)*axis.x+int64_t(axis.y)*axis.y;
  if(squared<=200*200){
    joystick_armed=true;
    if(!joystick_panning)return false;
    joystick_panning=false;joystick_velocity={};navigate(focus);return true;
  }
  if(!joystick_armed)return false;
  const float magnitude=std::hypot(float(axis.x),float(axis.y));
  const bool starting=!joystick_panning;
  const uint32_t gap=now-joystick_ms;
  const uint32_t elapsed=starting?20:gap<=50?gap:32; // Ordinary 30/40 ms frames retain real elapsed time; stalls stay bounded.
  joystick_ms=now;joystick_panning=true;
  if(starting){cancel_contact();moving=false;target_focus=focus;joystick_velocity={};}
  const float strength=std::clamp((magnitude-200.f)/800.f,0.f,1.f);
  const float speed=240.f*strength*std::sqrt(strength);
  // Exact first-order velocity response and its displacement integral keep
  // speed/acceleration independent of whether the caller runs at 30 or 100 Hz.
  const float seconds=elapsed/1000.f,decay=std::exp(-float(elapsed)/40.f);
  const auto target=DesktopPoint{axis.x/magnitude*speed,axis.y/magnitude*speed};
  const auto difference=DesktopPoint{joystick_velocity.x-target.x,joystick_velocity.y-target.y};
  const auto delta=DesktopPoint{target.x*seconds+difference.x*.04f*(1-decay),
      target.y*seconds+difference.y*.04f*(1-decay)};
  joystick_velocity={target.x+difference.x*decay,target.y+difference.y*decay};
  const auto previous=camera;
  pan_honeycomb(camera,-delta.x,-delta.y);
  if(previous.x!=camera.x||previous.y!=camera.y)status_navigation.activity(now);
  return starting||previous.x!=camera.x||previous.y!=camera.y;
}

void DesktopShell::nearest_focus() {
  if(!count)return;
  size_t best=focus;float score=distance(point(focus),camera);
  for(size_t i=0;i<count;++i){if(!launcher_visible(i))continue;float d=distance(point(i),camera);if(d<score-1e-5f){score=d;best=i;}}
  focus=best;
}
void DesktopShell::navigate(size_t app,int direction) {
  if(!count)return;
  app=std::min(app,count-1);
  if(!launcher_visible(app))return;
  if(!preferences.animations||!clock_ready){select(app);return;}
  if(preferences.layout==DesktopLayout::Honeycomb){
    target_focus=app;camera_from=camera;camera_target=point(app);motion_started=now_ms;
    moving=distance(camera,camera_target)>.01f;
  }else{
    const size_t size=launcher_page_size();
    // Moving within the destination page changes focus without truncating its
    // slide. A new page starts only after the previous destination is settled.
    if(moving&&!page_drag&&app/size==target_focus/size){focus=target_focus=app;return;}
    if(moving||page_drag)settle_motion();
    const size_t old_page=focus/size,new_page=app/size;
    target_focus=focus=app;
    if(old_page!=new_page){
      source_page=old_page;destination_page=new_page;offset_from=page_offset=0;
      offset_target=(direction?direction:(new_page>old_page?1:-1))>0?-float(canvas_layout::side):float(canvas_layout::side);
      page_direction=offset_target<0?1:-1;
      motion_started=now_ms;moving=true;
    }
  }
}
bool DesktopShell::tick(uint32_t milliseconds) {
  now_ms=milliseconds;clock_ready=true;
  const bool status_changed=status_navigation.update(now_ms,preferences.auto_hide_status,
      status_navigation.pinned,status_navigation.low_battery);
  const bool workspace_changed=(lunaros::graphics::metrics::native&&home.enabled&&home.tick(*this,now_ms))||(!lunaros::graphics::metrics::native&&compact.enabled&&compact.tick(*this,now_ms));
  const auto poll=desktop_surface_extension(*this,surface).poll;
  const bool extension_changed=poll&&poll(*this,now_ms);
  const bool home_changed=home_navigation.tick(now_ms,preferences.animations);
  if(home_navigation.take_finished()){command(DesktopCommand::Home,true);home_navigation.reset();return true;}
  const bool edge_changed=edge_navigation.tick(now_ms,preferences.animations);
  if(edge_navigation.take_finished()){command(DesktopCommand::Back,true);return true;}
  if(!moving)return status_changed||home_changed||edge_changed||extension_changed||workspace_changed;
  if(static_cast<uint32_t>(now_ms-frame_ms)<16)return status_changed||home_changed||edge_changed||extension_changed||workspace_changed;
  frame_ms=now_ms;
  const auto elapsed=uint32_t(now_ms-motion_started);
  const float ease=lunaros::graphics::animation_progress(elapsed,lunaros::graphics::design::navigationMs);
  if(!grid_menu()&&preferences.layout==DesktopLayout::Honeycomb){
    camera={camera_from.x+(camera_target.x-camera_from.x)*ease,camera_from.y+(camera_target.y-camera_from.y)*ease};
    nearest_focus();
  }else page_offset=offset_from+(offset_target-offset_from)*ease;
  if(elapsed>=uint32_t(lunaros::graphics::design::navigationMs)){moving=false;page_offset=0;if(!grid_menu()&&preferences.layout==DesktopLayout::Honeycomb){camera=camera_target;focus=target_focus;}}
  return true;
}

DesktopPoint DesktopShell::logical(int x,int y) const {
  switch(preferences.rotation) { case 1:return {static_cast<float>(y),float(canvas_layout::last)-x}; case 2:return {float(canvas_layout::last)-x,float(canvas_layout::last)-y}; case 3:return {float(canvas_layout::last)-y,static_cast<float>(x)}; default:return {static_cast<float>(x),static_cast<float>(y)}; }
}
DesktopTile DesktopShell::tile(size_t app) const {
  if (app>=count) return {0,0,0,false,false};
  if(!launcher_visible(app))return {0,0,0,false,false};
  if(lunaros::graphics::metrics::native&&home.enabled&&home.layout){
    HomeItem item{apps[app].id};bool found=home.mode==HomeMode::AllApps;
    if(found){const unsigned size=home.layout->columns*home.layout->rows;item.page=uint8_t(app/size);item.column=uint8_t(app%size%home.layout->columns);item.row=uint8_t(app%size/home.layout->columns);}
    else for(const auto &entry:home.layout->items)if(entry.kind==HomeItemKind::Application&&entry.id==apps[app].id){item=entry;found=true;break;}
    if(!found)return {0,0,0,false,false};
    const auto b=home.box(*this,item);const auto d=std::min({64,b.width-16,b.height-24});
    return {float(b.x+b.width/2),float(b.y+(b.height-22)/2),float(d),home.visible(item.page),!home.animating&&!home.swiping};
  }
  if (preferences.layout==DesktopLayout::Honeycomb) {
    if constexpr(!lunaros::graphics::metrics::native){
      const auto p=point(app);const float dx=p.x-camera.x,dy=p.y-camera.y;
      const float x=canvas_layout::center+dx,y=(canvas_layout::side+launcher_top())/2.f+dy;
      const float radius=std::hypot(dx,dy);
      const float d=compact_honeycomb::icon_diameter(radius);
      const float r=std::max(22.f,d/2);
      return {x,y,d,x+d/2>0&&x-d/2<canvas_layout::side&&y+d/2>launcher_top()&&y-d/2<canvas_layout::side,
        x-r>=0&&x+r<=canvas_layout::side&&y-r>=launcher_top()&&y+r<=canvas_layout::side,honeycomb_detail(radius)};
    }
    auto p=point(app);const auto projection=project_honeycomb(p.x-camera.x,p.y-camera.y,preferences.show_labels);
    const float d=projection.diameter,x=canvas_layout::center+projection.x,y=(canvas_layout::side+launcher_top())/2.f+projection.y,r=std::max(22.f,d/2);
    return {x,y,d,x+d/2>0&&x-d/2<canvas_layout::side&&y+d/2>launcher_top()&&y-d/2<canvas_layout::side,d>=honeycomb_layout::minimumTapSize&&x-r>=0&&x+r<=canvas_layout::side&&y-r>=launcher_top()&&y+r<=canvas_layout::side,projection.detail};
  }
  if constexpr(lunaros::graphics::metrics::native) {
    const size_t size=launcher_page_size(),page=app/size;
    float offset=0;bool visible=page==focus/size;
    if(moving||page_drag){
      visible=page==source_page||page==destination_page;
      offset=page_offset+(page==destination_page&&destination_page!=source_page?float(canvas_layout::side)*page_direction:0);
    }
    const bool single=preferences.layout==DesktopLayout::Single;
    const auto columns=single?1:grid_columns,rows=single?1:grid_rows;
    const auto &g=compiled_layout::grid(columns,rows,preferences.auto_hide_status,preferences.show_labels);
    const auto index=app%size;
    const float x=g.x[index%columns]+offset,y=g.y[index/columns];
    return {x,y,float(g.diameter),visible&&x+g.diameter/2>0&&x-g.diameter/2<canvas_layout::side,visible&&!moving&&!page_drag};
  } else return {0,0,0,false,false};
}

bool DesktopShell::pan_honeycomb(DesktopPoint anchor,float dx,float dy){
  const auto previous=camera;const auto previous_focus=focus;float minx=0,maxx=0,miny=0,maxy=0;bool first=true;
  for(size_t i=0;i<count;++i)if(launcher_visible(i)){const auto p=point(i);if(first){minx=maxx=p.x;miny=maxy=p.y;first=false;}else{minx=std::min(minx,p.x);maxx=std::max(maxx,p.x);miny=std::min(miny,p.y);maxy=std::max(maxy,p.y);}}
  if(first)return false;
  camera={std::clamp(anchor.x-dx,minx,maxx),std::clamp(anchor.y-dy,miny,maxy)};nearest_focus();target_focus=focus;
  return previous.x!=camera.x||previous.y!=camera.y||previous_focus!=focus;
}

} // namespace lunaredu::desktop
