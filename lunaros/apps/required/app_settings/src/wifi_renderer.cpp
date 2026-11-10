#include "lunaros_ui_metrics.h"
#include "lunaros_settings_wifi.h"
#include "desktop/graphics/page.h"
#include "desktop/graphics/scene_icon.h"
#include "desktop/shell/shell.h"
#include "desktop/ui/content_layout.h"
#include <cstdio>
#include <cstring>
namespace lunaredu::settings {
namespace ui=::lunaredu::lunaros::graphics::metrics;
using namespace desktop;
using namespace lunaros::graphics;
namespace {
constexpr uint16_t white=theme::text,muted=theme::appsecondary;
constexpr uint32_t wifi_id=3000,list_id=5000,scroll_id=5001,keypad_id=5002;
void label(Scene &scene,uint32_t id,int y,const char *text,unsigned size=desktop::typography::label.size,uint16_t ink=white){scene.label(id,{ui::px(8),y,ui::px(224),ui::px(28)},text,size,ink);}
void wifi(Scene &scene,const DesktopShell &shell){
  const ContentLayout layout(shell.preferences.auto_hide_status);
  if(!wifi_browser(shell)){page::content_state(scene,layout,page::State::Loading,"正在读取网络状态");return;}
  const auto &s=*wifi_browser(shell);
  if(s.view==WifiView::Password){
    scene.label(40,{ui::px(8),ui::px(31),ui::px(224),ui::px(20)},s.selected.ssid,desktop::typography::label.size,white);
    char masked[65]{};for(unsigned i=0;i<s.input.length;++i)masked[i]=s.input.revealed||(s.input.last_visible&&i+1==s.input.length)?s.input.value[i]:'*';
    scene.textbox(41,{ui::px(8),ui::px(54),ui::px(224),ui::px(30)},masked,16,false);wipe_text(masked,sizeof(masked));
    TextInputKeys keys;const size_t count=text_input_keys(s.input,keys);const char *labels[16]{};int disabled=-1;
    for(size_t i=0;i<count;++i){labels[i]=keys[i].label;if(keys[i].action==TextInputAction::Done&&!wifi_password_valid(s))disabled=int(i);}
    scene.keypad(keypad_id,{ui::px(5),ui::px(86),ui::px(230),ui::px(126)},labels,count,text_input_columns(s.input),s.input.focus,disabled);return;
  }
  WifiButtons buttons;const size_t count=wifi_buttons(s,buttons);
  if constexpr(ui::native){
    if(s.view==WifiView::List){
      const WifiListLayout list(shell.preferences.auto_hide_status);
      constexpr int h=ui::row_min,third=(ContentLayout::width-ContentLayout::gap*2)/3;
      bool current=false;unsigned nearby=0;
      scene.label(42,{ui::inset,layout.top+h+ui::gap,ContentLayout::width-third-ui::gap,h},"附近网络",ui::font(16),muted,Align::Left);
      scene.container(list_id,{ui::inset,list.top(),ContentLayout::width,list.height()},s.scroll*list.pitch(),scroll_id);
      scene.flex_layout(list_id,false,list.pitch()-list.row_height());
      for(size_t i=0;i<count;++i){const auto &b=buttons[i];
        if(b.action==WifiControl::Current){
          current=true;page::row(scene,wifi_id+i,{ui::inset,layout.top,ContentLayout::width,h},b.label,"当前",shell.keys_enabled&&s.focus==i,true,0,theme::Tone::Normal,48);
          desktop::scene_icon(scene,6100+i*3,{ui::inset,(h-ui::row_icon)/2,ui::row_icon,ui::row_icon},lunaros::icons::Icon::Wifi,theme::mint,wifi_id+i,shell.preferences.antialias);
          scene.overlay(6100+i*3,{ui::inset,(h-ui::row_icon)/2,ui::row_icon,ui::row_icon});
        }else if(b.action==WifiControl::Scan){
          scene.button(wifi_id+i,{ui::side-ui::inset-third,layout.top+h+ui::gap,third,h},b.label,ui::font(14),theme::appsurface,white,shell.keys_enabled&&s.focus==i,s.content->scan!=WifiScan::Scanning);
        }else if(b.action==WifiControl::Network){const auto &ap=s.content->networks[b.index];
          page::row(scene,wifi_id+i,{0,0,ContentLayout::width,list.row_height()},ap.ssid,!ap.supported?"不支持":ap.secure?"加密":"开放",shell.keys_enabled&&s.focus==i,true,list_id,theme::Tone::Normal,48);
          scene.flow_item(wifi_id+i,0,nearby++);
          desktop::scene_icon(scene,6100+i*3,{ui::inset,(list.row_height()-ui::row_icon)/2,ui::row_icon,ui::row_icon},lunaros::icons::Icon::Wifi,theme::appaccent,wifi_id+i,shell.preferences.antialias);
          scene.overlay(6100+i*3,{ui::inset,(list.row_height()-ui::row_icon)/2,ui::row_icon,ui::row_icon});
        }
      }
      if(!current)page::row(scene,49,{ui::inset,layout.top,ContentLayout::width,h},"当前网络","未连接",false,false,0,theme::Tone::Normal,12);
      if(!nearby)scene.label(43,{ui::inset,list.top(),ContentLayout::width,list.height()},s.content&&s.content->scan==WifiScan::Scanning?"正在扫描附近网络":s.content&&s.content->scan==WifiScan::Failed?"扫描失败，请重试":"点击扫描查找网络",ui::font(14),muted);
      return;
    }
  }
  if(s.view==WifiView::List){
    const WifiListLayout list(shell.preferences.auto_hide_status);
    bool current=false;unsigned nearby=0;
    scene.label(42,{ui::px(8),layout.top+ui::px(64),ui::px(144),ui::px(24)},"附近网络",ui::font(14),muted,Align::Left);
    scene.container(list_id,{ui::px(8),list.top(),ui::px(224),list.height()},s.scroll*list.pitch(),scroll_id);
    scene.flex_layout(list_id,false,list.pitch()-list.row_height());
    for(size_t i=0;i<count;++i){const auto &b=buttons[i];
      if(b.action==WifiControl::Current){
        current=true;page::row(scene,wifi_id+i,{ui::px(8),layout.top,ui::px(224),ui::px(56)},b.label,"当前",shell.keys_enabled&&s.focus==i,true,0,theme::Tone::Normal,48);
        page::icon_badge(scene,6100+i*3,{ui::px(8),ui::px(13),ui::px(30),ui::px(30)},lunaros::icons::Icon::Wifi,theme::mint,wifi_id+i,shell.preferences.antialias);
      }else if(b.action==WifiControl::Scan){
        scene.button(wifi_id+i,{ui::px(168),layout.top+ui::px(64),ui::px(64),ui::px(24)},b.label,ui::font(14),theme::appsurface,white,shell.keys_enabled&&s.focus==i,s.content->scan!=WifiScan::Scanning);
      }else if(b.action==WifiControl::Network){
        const auto &ap=s.content->networks[b.index];
        page::row(scene,wifi_id+i,{0,0,ui::px(224),list.row_height()},ap.ssid,!ap.supported?"不支持":ap.secure?"加密":"开放",shell.keys_enabled&&s.focus==i,true,list_id,theme::Tone::Normal,48);
        scene.flow_item(wifi_id+i,0,nearby++);
        page::icon_badge(scene,6100+i*3,{ui::px(8),(list.row_height()-ui::px(30))/2,ui::px(30),ui::px(30)},lunaros::icons::Icon::Wifi,theme::appaccent,wifi_id+i,shell.preferences.antialias);
      }
    }
    if(!current)page::row(scene,49,{ui::px(8),layout.top,ui::px(224),ui::px(56)},"当前网络","未连接",false,false,0,theme::Tone::Normal,12);
    if(!nearby)scene.label(43,{ui::px(8),list.top(),ui::px(224),list.height()},s.content&&s.content->scan==WifiScan::Scanning?"正在扫描附近网络":s.content&&s.content->scan==WifiScan::Failed?"扫描失败，请重试":"点击扫描查找网络",ui::font(14),muted);
    return;
  }
  label(scene,40,ui::px(42),s.selected.ssid);
  const char *message=s.view==WifiView::Review?"确认连接此网络":s.view==WifiView::Connected?(s.message[0]?s.message:"已连接"):s.view==WifiView::Forget?"忘记已保存的网络？":s.view==WifiView::Connecting?"正在连接":s.message;
  scene.label(41,{ui::px(8),ui::px(80),ui::px(224),ui::px(24)},message,desktop::typography::label.size,s.view==WifiView::Failed?theme::warning:muted);
  if(s.view==WifiView::Connecting)label(scene,42,ui::px(121),"请稍候，正在建立连接",ui::font(12),muted);
  for(size_t i=0;i<count;++i){const auto &b=buttons[i];const bool connect=b.action==WifiControl::Connect||b.action==WifiControl::Retry;
    const bool enabled=!connect||(s.content&&s.content->available&&s.content->editable&&s.selected.supported&&wifi_password_valid(s));
    page::stacked_action(scene,{wifi_id+uint32_t(i),b.label,enabled,b.action==WifiControl::Forget?theme::Tone::Danger:theme::Tone::Accent},i,count,shell.keys_enabled&&s.focus==i);
  }
}
}
void build_wifi_surface(Scene &scene,const DesktopShell &s,const DesktopStatus &) {wifi(scene,s);}
bool apply_wifi_action(DesktopShell &s,Action a) {
 if(a.id==scroll_id)return wifi_scroll(s,unsigned(std::max<int32_t>(0,a.value))/WifiListLayout(s.preferences.auto_hide_status).pitch());
 if(a.id==keypad_id)return a.value>=0&&wifi_activate(s,unsigned(a.value));
 if(a.id>=wifi_id&&a.id<wifi_id+27)return wifi_activate(s,a.id-wifi_id);
 return false;
}
}
