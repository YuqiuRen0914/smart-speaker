#include "internal.h"
namespace lunaredu::desktop {
namespace {
int hit_app(const DesktopShell &s,int x,int y){
  for(size_t i=0;i<s.count;++i){const auto t=s.tile(i);if(t.visible&&std::hypot(t.x-x,t.y-y)<=22)return int(i);}return -1;
}
void finish(CompactWorkspace &c){c.cancel();c.mode=CompactMode::Desktop;c.selected.clear();c.error.clear();}
void show_apps(CompactWorkspace &c,const DesktopShell &s){const auto index=s.find_app(c.selected.c_str());c.mode=CompactMode::AllApps;c.page=index<s.count?unsigned(index/3):0;c.row=index<s.count?unsigned(index%3):0;}
}
void CompactWorkspace::choose(DesktopShell &s,unsigned item){
  error.clear();const auto index=s.find_app(selected.c_str());
  if(mode==CompactMode::Menu){
    if(item>1)return;
    page=row=0;mode=item==0?CompactMode::AllApps:CompactMode::Appearance;
  }else if(mode==CompactMode::ItemActions&&index<s.count){
    if(item==0)mode=CompactMode::Edit;
    else if(item==1){hide(s,selected.c_str());mode=CompactMode::Edit;selected.clear();}
    else if(s.apps[index].uninstallable){pending_uninstall=selected;uninstall_return=mode;mode=CompactMode::Uninstall;page=row=0;}
    else finish(*this);
  }else if(mode==CompactMode::AllApps&&page*3+item<s.count){
    mode=CompactMode::Desktop;shell_detail::enter(s,page*3+item);
  }else if(mode==CompactMode::AppActions&&index<s.count){
    if(item==0){const bool changed=visible(selected.c_str())?hide(s,selected.c_str()):restore(s,selected.c_str());if(changed)show_apps(*this,s);else error="目录准备中，稍后重试";}
    else if(item==1){mode=CompactMode::Desktop;shell_detail::enter(s,index);}
    else if(s.apps[index].uninstallable){pending_uninstall=selected;uninstall_return=mode;mode=CompactMode::Uninstall;page=row=0;}
    else show_apps(*this,s);
  }else if(mode==CompactMode::Appearance){
    if(item==0){s.preferences.wallpaper=DesktopWallpaper((unsigned(s.preferences.wallpaper)+1)%4);s.preferences.wallpaper_path[0]=0;s.wallpaper_image.reset();s.preferences_changed=true;}
    else if(item==1){s.preferences.animations=!s.preferences.animations;s.preferences_changed=true;}
    else{const auto settings=s.settings_id?s.find_app(s.settings_id):s.count;if(settings<s.count){mode=CompactMode::Desktop;shell_detail::enter(s,settings);}else error="设置应用不可用";}
  }else if(mode==CompactMode::Uninstall){
    if(item==0){s.action="desktop.uninstall";mode=CompactMode::Edit;}
    else{mode=uninstall_return;page=row=0;pending_uninstall.clear();}
  }
}
bool CompactWorkspace::command(DesktopShell &s,unsigned value){
  auto cmd=DesktopCommand(value);
  if(cmd==DesktopCommand::Home){const bool changed=mode!=CompactMode::Desktop;finish(*this);return changed;}
  if(cmd==DesktopCommand::System){finish(*this);return false;}
  if(mode==CompactMode::Desktop)return false;
  if(cmd>=DesktopCommand::Left&&cmd<=DesktopCommand::Down){
    const unsigned physical=cmd==DesktopCommand::Right?0:cmd==DesktopCommand::Down?1:cmd==DesktopCommand::Left?2:3;
    constexpr DesktopCommand directions[]={DesktopCommand::Right,DesktopCommand::Down,DesktopCommand::Left,DesktopCommand::Up};
    cmd=directions[(physical+4-s.preferences.rotation)%4];
  }
  if(contact||s.contact)s.cancel_contact();
  if(cmd==DesktopCommand::Back){
    cancel();error.clear();
    if(mode==CompactMode::AllApps||mode==CompactMode::Appearance){mode=CompactMode::Menu;page=row=0;}
    else if(mode==CompactMode::AppActions)show_apps(*this,s);
    else if(mode==CompactMode::ItemActions)mode=CompactMode::Edit;
    else if(mode==CompactMode::Uninstall){mode=uninstall_return;page=row=0;pending_uninstall.clear();}
    else finish(*this);
    return true;
  }
  if(mode==CompactMode::Edit){if(cmd==DesktopCommand::Confirm){finish(*this);return true;}return false;}
  if(cmd==DesktopCommand::Confirm){choose(s,row);return true;}
  if(cmd==DesktopCommand::Next||cmd==DesktopCommand::Previous||cmd==DesktopCommand::Up||cmd==DesktopCommand::Down||cmd==DesktopCommand::Left||cmd==DesktopCommand::Right){
    const unsigned count=mode==CompactMode::AllApps?unsigned(s.count):mode==CompactMode::Uninstall||mode==CompactMode::Menu?2:3;
    if(!count)return true;
    const int delta=cmd==DesktopCommand::Previous||cmd==DesktopCommand::Up||cmd==DesktopCommand::Left?-1:1;
    const unsigned index=unsigned((int(page*3+row)+int(count)+delta)%int(count));page=index/3;row=index%3;return true;
  }return false;
}
bool CompactWorkspace::pointer(DesktopShell &s,bool pressed,bool valid,int x,int y){
  if(!enabled||!valid)return false;
  if(mode==CompactMode::Desktop)return false;
  if(pressed&&!contact){
    contact=true;start_x=last_x=x;start_y=last_y=y;started=s.now_ms;dragging=long_pressed=false;
    camera_x=s.camera.x;camera_y=s.camera.y;pressed_app=mode==CompactMode::Edit&&y>=s.launcher_top()+24?hit_app(s,x,y):-1;
    if(mode==CompactMode::AllApps&&y>=s.launcher_top()+32)row=unsigned(std::clamp((y-s.launcher_top()-32)/56,0,2));
    return true;
  }
  if(!contact)return true;
  if(pressed){last_x=x;last_y=y;const int dx=x-start_x,dy=y-start_y;
    if(mode==CompactMode::Edit&&std::hypot(float(dx),float(dy))>=8){
      if(pressed_app>=0){dragging=true;selected=s.apps[pressed_app].id;}
      else s.pan_honeycomb({camera_x,camera_y},float(dx),float(dy));
    }return true;
  }
  x=last_x;y=last_y;const int dx=x-start_x,dy=y-start_y;contact=false;
  if(long_pressed){long_pressed=false;return true;}
  if(dragging){
    const auto target=hit_app(s,x,y);const std::string source=selected;
    if(target<0||!reorder(s,source.c_str(),s.apps[target].id))error="请放到同类应用的位置";
    else error.clear();
    cancel();return true;
  }
  if(mode==CompactMode::Edit){
    if(std::hypot(float(dx),float(dy))>=8){s.navigate(s.focus);return true;}
    if(y<s.launcher_top()+24){if(x>=160)finish(*this);else{mode=CompactMode::Menu;page=row=0;}return true;}
    const auto index=hit_app(s,x,y);if(index>=0){selected=s.apps[index].id;mode=CompactMode::ItemActions;row=page=0;}else{mode=CompactMode::Menu;page=row=0;}return true;
  }
  if(std::abs(dx)>=32&&std::abs(dx)>std::abs(dy)&&mode==CompactMode::AllApps){
    const unsigned pages=std::max(1u,(unsigned(s.count)+2)/3);
    page=unsigned(std::clamp(int(page)+(dx<0?1:-1),0,int(pages)-1));row=0;return true;
  }
  if(std::hypot(float(dx),float(dy))>=8)return true;
  if(y<s.launcher_top()+28){if(x<70)command(s,unsigned(DesktopCommand::Back));else if(x>=174&&mode==CompactMode::AllApps){page=(page+1)%std::max(1u,(unsigned(s.count)+2)/3);row=0;}return true;}
  const int items=mode==CompactMode::Menu||mode==CompactMode::Uninstall?2:3;
  const int item=(y-s.launcher_top()-32)/56;if(item>=0&&item<items&&y<s.launcher_top()+32+items*56){row=unsigned(item);choose(s,row);}return true;
}
}
