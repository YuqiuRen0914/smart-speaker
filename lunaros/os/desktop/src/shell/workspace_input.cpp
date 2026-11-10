#include "internal.h"
#include "desktop/shell/workspace.h"
namespace lunaredu::desktop {
namespace {
int panel_row(const DesktopShell &s,int y){return (y-s.launcher_top()-64)/52;}
void finish(HomeWorkspace &h){if(h.mode==HomeMode::AllApps||h.mode==HomeMode::DrawerActions)h.page=std::min<uint8_t>(h.return_page,h.layout->pages-1);h.cancel();h.mode=HomeMode::Desktop;h.selected=-1;h.error.clear();}
int drawer_hit(const DesktopShell &s,int x,int y){
  const auto &h=s.home;const auto first=home_cell(h.layout->columns,h.layout->rows,true,0,0);
  if(x<first.x||y<first.y||y>=canvas_layout::side-28)return -1;
  const int c=(x-first.x)/(first.width+12),r=(y-first.y)/(first.height+12);
  if(c>=h.layout->columns||r>=h.layout->rows)return -1;
  const auto index=size_t(h.page)*h.layout->columns*h.layout->rows+size_t(r)*h.layout->columns+size_t(c);
  return index<s.count?int(index):-1;
}
}
bool HomeWorkspace::command(DesktopShell &s,unsigned value){
  const auto cmd=DesktopCommand(value);
  if(cmd==DesktopCommand::Home){finish(*this);page=std::min<uint8_t>(page,layout->pages-1);return true;}
  if(cmd==DesktopCommand::Back){
    if(mode==HomeMode::Desktop)return false;
    if(mode==HomeMode::DrawerActions){mode=HomeMode::AllApps;cancel();return true;}
    if(mode==HomeMode::PageActions){mode=HomeMode::Pages;cancel();return true;}
    if(mode==HomeMode::Uninstall){mode=selected>=0?HomeMode::ItemActions:HomeMode::DrawerActions;cancel();return true;}
    if(mode==HomeMode::AllApps||mode==HomeMode::Edit)finish(*this);else{cancel();mode=HomeMode::Edit;error.clear();}return true;
  }
  if(mode!=HomeMode::Desktop&&mode!=HomeMode::AllApps&&mode!=HomeMode::Edit)return false;
  if(cmd==DesktopCommand::Left||cmd==DesktopCommand::Previous){turn(s,-1);return true;}
  if(cmd==DesktopCommand::Right||cmd==DesktopCommand::Next){turn(s,1);return true;}
  if(cmd==DesktopCommand::Confirm){if(mode==HomeMode::Edit){finish(*this);return true;}if(s.focus<s.count){shell_detail::enter(s,s.focus);return true;}}
  return false;
}
bool HomeWorkspace::pointer(DesktopShell &s,bool pressed,bool valid,int x,int y){
  if(!valid){const bool changed=contact||dragging||swiping;cancel();return changed;}
  if(pressed&&!contact){
    if(animating)return false;
    contact=true;start_x=last_x=x;start_y=last_y=y;started=s.now_ms;long_pressed=false;
    pressed_item=(mode==HomeMode::Desktop||mode==HomeMode::Edit)?hit(s,x,y):-1;
    if(mode==HomeMode::AllApps)drawer_selected=drawer_hit(s,x,y);
    return false;
  }
  if(!contact)return false;
  const int dx=x-start_x,dy=y-start_y;
  if(pressed){last_x=x;last_y=y;
    if(mode==HomeMode::Edit&&pressed_item>=0&&std::hypot(float(dx),float(dy))>=12){dragging=true;drag_item=pressed_item;selected=drag_item;drag_x=float(x);drag_y=float(y);return true;}
    if((mode==HomeMode::Desktop||mode==HomeMode::AllApps||mode==HomeMode::Edit)&&!long_pressed&&!dragging&&std::abs(dx)>12&&std::abs(dx)>std::abs(dy)){
      const unsigned pages=mode==HomeMode::AllApps?(unsigned(s.count)+layout->columns*layout->rows-1)/(layout->columns*layout->rows):layout->pages;
      source_page=page;destination_page=uint8_t(std::clamp(int(page)+(dx<0?1:-1),0,int(pages)-1));
      offset=std::clamp(float(dx),-float(canvas_layout::side),float(canvas_layout::side))*(source_page==destination_page?.2f:1.f);swiping=true;return true;
    }return false;
  }
  contact=false;
  if(dragging&&drag_item>=0){
    const auto item=drop_target(x,y);
    if(!place(item,drag_item))error="这里没有足够空间";
    cancel();return true;
  }
  if(swiping){
    const bool commit=std::abs(dx)>=48&&source_page!=destination_page;
    offset_from=offset;offset_to=commit?(destination_page>source_page?-float(canvas_layout::side):float(canvas_layout::side)):0;
    if(commit)page=destination_page;
    motion_started=s.now_ms;swiping=false;animating=s.preferences.animations;
    if(!animating)offset=0;
    return true;
  }
  if(long_pressed){long_pressed=false;return true;}
  if(std::hypot(float(dx),float(dy))>=24){
    if(mode==HomeMode::Desktop&&start_y>=canvas_layout::side-40&&dy<-48&&std::abs(dy)>std::abs(dx)){return_page=page;mode=HomeMode::AllApps;page=0;return true;}
    return false;
  }
  if(mode==HomeMode::Desktop){
    if(y>=canvas_layout::side-26){mode=HomeMode::Pages;panel_page=0;return true;}
    const int item=hit(s,x,y);if(item<0)return false;const auto &entry=layout->items[size_t(item)];
    const char *app=entry.id.c_str();if(entry.kind==HomeItemKind::Widget){const auto *d=definition(app);app=d?(d->route==HomeWidgetRoute::Settings?s.settings_id:d->application):nullptr;}
    if(app){const auto index=s.find_app(app);if(index<s.count){shell_detail::enter(s,index);return true;}}return false;
  }
  if(mode==HomeMode::AllApps){
    const auto index=drawer_hit(s,x,y);if(index>=0){shell_detail::enter(s,size_t(index));return true;}return false;
  }
  if(mode==HomeMode::Edit){
    if(y>=canvas_layout::side-42){const int button=x/(canvas_layout::side/4);
      if(button==0){mode=HomeMode::Widgets;panel_page=0;}
      else if(button==1){mode=HomeMode::Pages;panel_page=0;}
      else if(button==2)mode=HomeMode::Appearance;
      else finish(*this);
      return true;
    }
    selected=hit(s,x,y);if(selected>=0)mode=HomeMode::ItemActions;return true;
  }
  if(y<s.launcher_top()+56){
    if(x<canvas_layout::side/3){mode=mode==HomeMode::DrawerActions?HomeMode::AllApps:mode==HomeMode::PageActions?HomeMode::Pages:HomeMode::Edit;error.clear();}
    else if(x>canvas_layout::side*2/3){size_t count=0;system_home_widgets(count);const size_t pages=mode==HomeMode::Widgets?(count+widget_count+5)/6:(layout->pages+3)/4;panel_page=uint8_t((panel_page+1)%std::max<size_t>(1,pages));}
    return true;
  }
  const int row=panel_row(s,y);if(row<0||row>=6)return false;
  if(mode==HomeMode::Widgets){
    size_t n=0;const auto *system=system_home_widgets(n);const size_t index=panel_page*6+unsigned(row);
    const auto *d=index<n?system+index:index<n+widget_count?widgets+(index-n):nullptr;
    if(d){if(add_widget(d->id)){mode=HomeMode::Edit;error.clear();}else error="组件已添加，或没有可用空间";}return true;
  }
  if(mode==HomeMode::Pages){
    const unsigned index=panel_page*4+unsigned(row);
    if(row<4&&index<layout->pages){page=uint8_t(index);mode=x>=canvas_layout::side-100?HomeMode::PageActions:HomeMode::Desktop;}
    else if(row==4){if(!add_page())error="已达到页面上限";}
    else if(row==5){if(!delete_page(page))error="无法移除：请先腾出其他页面的空间";else mode=HomeMode::Edit;}
    return true;
  }
  if(mode==HomeMode::PageActions){
    if(row==0){if(page>0)move_page(page,page-1);}
    else if(row==1){if(page+1<layout->pages)move_page(page,page+1);}
    else if(row==2){if(!delete_page(page))error="请先腾出其他页面的空间";else mode=HomeMode::Pages;}
    else if(row==3){if(arrange())mode=HomeMode::Edit;}
    return true;
  }
  if(mode==HomeMode::DrawerActions&&drawer_selected>=0&&size_t(drawer_selected)<s.count){
    const auto &app=s.apps[drawer_selected];
    if(row==0){if(add_application(app.id,return_page)){const auto found=std::find_if(layout->items.begin(),layout->items.end(),[&](const HomeItem &i){return i.kind==HomeItemKind::Application&&i.id==app.id;});page=found->page;mode=HomeMode::Edit;error.clear();}else error="应用已在桌面，或没有可用空间";}
    else if(row==1)shell_detail::enter(s,size_t(drawer_selected));
    else if(row==2&&app.uninstallable){pending_uninstall=app.id;mode=HomeMode::Uninstall;}
    return true;
  }
  if(mode==HomeMode::Appearance){
    if(row<4){constexpr unsigned c[]={3,4,5,5},r[]={3,4,4,5};if(!density(c[row],r[row]))error="布局无法容纳现有组件";else error.clear();}
    else if(row==4){s.preferences.wallpaper=DesktopWallpaper((unsigned(s.preferences.wallpaper)+1)%4);s.preferences.wallpaper_path[0]=0;s.wallpaper_image.reset();s.preferences_changed=true;}
    else{s.preferences.animations=!s.preferences.animations;s.preferences_changed=true;}return true;
  }
  if(mode==HomeMode::ItemActions&&selected>=0&&size_t(selected)<layout->items.size()){
    const auto entry=layout->items[size_t(selected)];const auto index=s.find_app(entry.id.c_str());
    if(row==0){mode=HomeMode::Edit;}
    else if(row==1){remove(size_t(selected));mode=HomeMode::Edit;}
    else if(row==2&&entry.kind==HomeItemKind::Widget){const auto *d=definition(entry.id.c_str());if(d&&d->alternate_height){auto resized=entry;resized.height=entry.height==d->height?d->alternate_height:d->height;if(!place(resized,selected,false))error="调整尺寸需要更多空间";else mode=HomeMode::Edit;}}
    else if(row==2&&entry.kind==HomeItemKind::Application&&index<s.count&&s.apps[index].uninstallable){pending_uninstall=entry.id;mode=HomeMode::Uninstall;}
    return true;
  }
  if(mode==HomeMode::Uninstall&&!pending_uninstall.empty()){
    if(row==0){s.action="desktop.uninstall";page=std::min<uint8_t>(page,layout->pages-1);mode=HomeMode::Edit;}else if(row==1)mode=selected>=0?HomeMode::ItemActions:HomeMode::DrawerActions;return true;
  }
  return false;
}
}
