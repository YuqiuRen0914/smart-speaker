#include "lunaros_settings_wifi.h"
#include "desktop/shell/shell.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace lunaredu::desktop {
std::shared_ptr<const WifiBrowserState> wifi_browser(const DesktopShell &s) {
  return s.surface_state && s.surface_state->kind==&kWifiSurfaceStateKind ? std::static_pointer_cast<const WifiBrowserState>(s.surface_state):nullptr;
}
void publish_wifi_browser(DesktopShell &s,std::shared_ptr<const WifiBrowserState> value) { s.surface_state=std::move(value); }
namespace {
void message(WifiBrowserState &s,const char *value){std::snprintf(s.message,sizeof(s.message),"%s",value);}
void list(WifiBrowserState &s){s.view=WifiView::List;s.selected={};s.input.clear();s.focus=s.scroll=0;s.request=WifiRequest::None;message(s,"");++s.generation;}
void reveal_focus(WifiBrowserState &s,bool hidden){
  const unsigned visible=WifiListLayout(hidden).rows();
  WifiButtons buttons;const auto count=wifi_buttons(s,buttons);unsigned nearby=0;
  for(unsigned i=0;i<count;++i)if(buttons[i].action==WifiControl::Network){
    if(i==s.focus){if(nearby<s.scroll)s.scroll=nearby;if(nearby>=unsigned(s.scroll)+visible)s.scroll=nearby-visible+1;return;}
    ++nearby;
  }
}
bool activate(DesktopShell &shell,WifiBrowserState &s,const WifiButton &button){
  switch(button.action){
    case WifiControl::Current:
      if(!s.content||!*s.content->connected_ssid)return false;
      s.selected={};std::snprintf(s.selected.ssid,sizeof(s.selected.ssid),"%s",s.content->connected_ssid);
      s.selected.saved_index=s.content->connected_saved_index;s.view=WifiView::Connected;s.focus=0;return true;
    case WifiControl::Network:{
      if(!s.content || s.content->scan!=WifiScan::Ready || button.index>=s.content->count)return false;
      s.selected=s.content->networks[button.index];s.input.clear();s.focus=0;message(s,"");
      if(!std::strcmp(s.selected.ssid,s.content->connected_ssid))s.view=WifiView::Connected;
      else if(!s.content->editable){s.view=WifiView::Failed;message(s,"当前网络由系统固定管理");}
      else if(!s.selected.supported){s.view=WifiView::Failed;message(s,"此安全模式不支持本机连接");}
      else s.view=s.selected.secure&&s.selected.saved_index<0?WifiView::Password:WifiView::Review;
      return true;
    }
    case WifiControl::Scan:
      if(!s.content || !s.content->available || s.content->scan==WifiScan::Scanning)return false;
      s.request=WifiRequest::Scan;s.focus=0;message(s,"");++s.generation;return true;
    case WifiControl::Mobile:s.input.clear();shell.action="os.provision";return true;
    case WifiControl::Edit:s.selected.saved_index=-1;s.view=WifiView::Password;s.input.group=-1;s.input.focus=0;s.focus=0;message(s,"");return true;
    case WifiControl::Connect:case WifiControl::Retry:
      if(!s.content || !s.content->available || !s.content->editable || !s.selected.supported || !wifi_password_valid(s))return false;
      s.view=WifiView::Connecting;s.input.revealed=s.input.last_visible=false;s.focus=0;s.request=WifiRequest::Connect;message(s,"");++s.generation;return true;
    case WifiControl::Networks:list(s);return true;
    case WifiControl::Forget:
      if(!s.content || !s.content->editable || s.selected.saved_index<0)return false;
      s.view=WifiView::Forget;s.focus=0;return true;
    case WifiControl::Cancel:s.view=WifiView::Connected;s.focus=0;return true;
    case WifiControl::ConfirmForget:
      if(!s.content || !s.content->editable)return false;
      s.request=WifiRequest::Forget;s.view=WifiView::Connecting;s.focus=0;message(s,"正在忘记网络");++s.generation;return true;
  }
  return false;
}
bool editor_key(WifiBrowserState &s,size_t index,uint32_t now){
  TextInputKeys keys;const size_t count=text_input_keys(s.input,keys);if(index>=count)return false;
  s.input.focus=static_cast<uint8_t>(index);
  if(keys[index].action==TextInputAction::Done){
    if(!wifi_password_valid(s))return false;
    s.view=WifiView::Review;s.input.revealed=s.input.last_visible=false;s.focus=0;return true;
  }
  return text_input_key(s.input,keys[index],now);
}
}
bool wifi_password_valid(const WifiBrowserState &s){
  if(!s.selected.secure || s.selected.saved_index>=0)return true;
  if(s.input.length>=8 && s.input.length<=63)return true;
  if(s.input.length!=64)return false;
  for(char c:s.input.value)if(c && !((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return false;
  return true;
}
size_t wifi_buttons(const WifiBrowserState &s,WifiButtons &buttons){
  buttons={};size_t count=0;
  const auto add=[&](WifiControl action,const char *label,uint8_t index=0){buttons[count++]={action,index,label};};
  if(s.view==WifiView::List){
    if(s.content&&*s.content->connected_ssid)add(WifiControl::Current,s.content->connected_ssid);
    if(s.content && s.content->available)add(WifiControl::Scan,s.content->scan==WifiScan::Scanning?"扫描中":s.content->scan==WifiScan::Ready?"重扫":"扫描");
    if(s.content && s.content->scan==WifiScan::Ready)
      for(uint8_t i=0;i<s.content->count;++i)if(std::strcmp(s.content->networks[i].ssid,s.content->connected_ssid))add(WifiControl::Network,s.content->networks[i].ssid,i);
  }else if(s.view==WifiView::Review){
    if(s.selected.secure && s.selected.saved_index<0)add(WifiControl::Edit,"编辑密码");
    add(WifiControl::Connect,"连接");
  }else if(s.view==WifiView::Failed){
    if(!s.selected.supported || (s.content && !s.content->editable)){
      add(WifiControl::Networks,"其他网络");add(WifiControl::Mobile,"手机配网");
    }else {
      if(s.selected.secure)add(WifiControl::Edit,"修改密码");
      add(WifiControl::Retry,"重试连接");
    }
  }else if(s.view==WifiView::Connected){
    add(WifiControl::Networks,"其他网络");
    if(s.content && s.content->editable && s.selected.saved_index>=0)add(WifiControl::Forget,"忘记网络");
  }else if(s.view==WifiView::Forget){
    add(WifiControl::Cancel,"取消");add(WifiControl::ConfirmForget,"确认忘记");
  }
  return count;
}
void open_desktop_wifi(DesktopShell &shell){
  close_desktop_wifi(shell);
  auto s=std::make_shared<WifiBrowserState>();s->generation=++shell.surface_generation;
  s->content=std::make_shared<WifiContent>();
  publish_wifi_browser(shell,std::move(s));shell.surface=DesktopSurface::Wifi;shell.row=shell.list_offset=0;shell.cancel_contact();
}
void close_desktop_wifi(DesktopShell &shell){
  if(wifi_browser(shell))shell.surface_generation=wifi_browser(shell)->generation+1;
  publish_wifi_browser(shell,{});
}
bool wifi_command(DesktopShell &shell,DesktopCommand command){
  if(!wifi_browser(shell))return false;
  if(command==DesktopCommand::Back && wifi_browser(shell)->view==WifiView::List){
    const auto *parent=shell.leaf_menu;const uint8_t row=shell.leaf_row;close_desktop_wifi(shell);
    if(parent && shell.open_menu(parent->id)){shell.row=row;shell.list_offset=row>=ContentLayout(shell.preferences.auto_hide_status).rows()?row-ContentLayout(shell.preferences.auto_hide_status).rows()+1:0;return true;}
    shell.surface=DesktopSurface::Launcher;return true;
  }
  auto s=std::make_shared<WifiBrowserState>(*wifi_browser(shell));bool changed=false;
  if(command==DesktopCommand::Back){
    if(s->view==WifiView::Password && text_input_back(s->input))changed=true;
    else if(s->view==WifiView::Review && s->selected.secure && s->selected.saved_index<0){s->view=WifiView::Password;changed=true;}
    else {list(*s);changed=true;}
  }else if(s->view==WifiView::Password){
    if(command==DesktopCommand::Confirm)changed=editor_key(*s,s->input.focus,shell.now_ms);
    else changed=text_input_move(s->input,command==DesktopCommand::Right?0:command==DesktopCommand::Down?1:command==DesktopCommand::Left?2:command==DesktopCommand::Up?3:-1,command==DesktopCommand::Previous?-1:1);
  }else {
    WifiButtons buttons;const size_t count=wifi_buttons(*s,buttons);
    if(count){
      s->focus=std::min<uint8_t>(s->focus,count-1);
      if(command==DesktopCommand::Confirm)changed=activate(shell,*s,buttons[s->focus]);
      else if(command!=DesktopCommand::Left && command!=DesktopCommand::Right){
        const int step=command==DesktopCommand::Previous||command==DesktopCommand::Up?-1:1;
        s->focus=static_cast<uint8_t>((s->focus+count+step)%count);reveal_focus(*s,shell.preferences.auto_hide_status);changed=true;
      }
    }
  }
  if(changed)publish_wifi_browser(shell,std::move(s));
  return changed;
}
bool wifi_activate(DesktopShell &shell,size_t index){
  if(!wifi_browser(shell))return false;
  auto s=std::make_shared<WifiBrowserState>(*wifi_browser(shell));bool changed=false;
  if(s->view==WifiView::Password)changed=editor_key(*s,index,shell.now_ms);
  else {
    WifiButtons buttons;const size_t count=wifi_buttons(*s,buttons);
    if(index>=count)return false;
    s->focus=static_cast<uint8_t>(index);changed=activate(shell,*s,buttons[index]);
  }
  if(changed)publish_wifi_browser(shell,std::move(s));
  return changed;
}
bool wifi_scroll(DesktopShell &shell,unsigned row){
  if(!wifi_browser(shell)||wifi_browser(shell)->view!=WifiView::List||!wifi_browser(shell)->content)return false;
  const auto &current=*wifi_browser(shell);WifiButtons available;const auto available_count=wifi_buttons(current,available);
  unsigned rows=0;for(unsigned i=0;i<available_count;++i)if(available[i].action==WifiControl::Network)++rows;
  const auto destination=static_cast<uint8_t>(std::min(row,rows>2?rows-2:0));
  unsigned index=0,focus=current.focus;
  for(unsigned i=0;i<available_count;++i)if(available[i].action==WifiControl::Network&&index++==destination){focus=i;break;}
  if(destination==current.scroll&&focus==current.focus)return false;
  auto s=std::make_shared<WifiBrowserState>(*wifi_browser(shell));
  s->scroll=destination;s->focus=static_cast<uint8_t>(focus);
  publish_wifi_browser(shell,std::move(s));return true;
}
bool wifi_tick(DesktopShell &shell,uint32_t now){
  if(!wifi_browser(shell) || wifi_browser(shell)->view!=WifiView::Password || !wifi_browser(shell)->input.last_visible || static_cast<uint32_t>(now-wifi_browser(shell)->input.last_character_ms)<700)return false;
  auto s=std::make_shared<WifiBrowserState>(*wifi_browser(shell));text_input_tick(s->input,now);publish_wifi_browser(shell,std::move(s));return true;
}
} // namespace lunaredu::desktop
