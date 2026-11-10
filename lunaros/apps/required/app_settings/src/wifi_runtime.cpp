#include "lunaros_settings_wifi_runtime.h"
#include "desktop/shell/shell.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace lunaredu::desktop {
namespace {
void fail(WifiBrowserState &s,const char *message){s.view=WifiView::Failed;s.request=WifiRequest::None;s.focus=0;s.input.revealed=s.input.last_visible=false;std::snprintf(s.message,sizeof(s.message),"%s",message);}
bool equal(const WifiContent &a,const WifiContent &b){
  if(a.count!=b.count || a.scan!=b.scan || a.available!=b.available || a.editable!=b.editable || a.connected_saved_index!=b.connected_saved_index || std::strcmp(a.connected_ssid,b.connected_ssid))return false;
  for(size_t i=0;i<a.count;++i){const auto &x=a.networks[i],&y=b.networks[i];if(std::strcmp(x.ssid,y.ssid)||x.rssi!=y.rssi||x.saved_index!=y.saved_index||x.secure!=y.secure||x.supported!=y.supported)return false;}
  return true;
}
}
bool WifiRuntime::update(DesktopShell &shell,uint32_t now,bool visible){
  // Hidden/blocked pages discard credential drafts. Recreate observations on
  // return instead of leaving a Wifi surface with no state forever.
  const bool reopened=visible && shell.surface==DesktopSurface::Wifi && !wifi_browser(shell);
  if(reopened)open_desktop_wifi(shell);
  const bool active=visible && shell.surface==DesktopSurface::Wifi && wifi_browser(shell);
  if(!active){
    if(active_ && port_.abandon)port_.abandon(port_.context);
    active_=connecting_=false;
    if(wifi_browser(shell) && !shell.home_navigation.exiting()){close_desktop_wifi(shell);return true;}
    return false;
  }
  const auto &published=*wifi_browser(shell);
  const bool new_generation=!active_ || published.generation!=generation_;
  if(!new_generation && published.request==WifiRequest::None && static_cast<uint32_t>(now-last_poll_)<125)return wifi_tick(shell,now);
  if(new_generation && active_ && port_.abandon)port_.abandon(port_.context);
  if(new_generation)connecting_=false;
  active_=true;generation_=published.generation;last_poll_=now;
  const WifiObservation observation=port_.observe?port_.observe(port_.context):WifiObservation{};
  // Only publish a heap-owned immutable snapshot after an actual change.
  // This bounded draft excludes the separately owned scan array; its input
  // destructor wipes the local credential copy on every exit.
  static_assert(sizeof(WifiBrowserState)<=512,"WiFi observation draft must stay bounded");
  WifiBrowserState draft=published;auto *s=&draft;bool changed=false;
  WifiContent content=observation.content;content.count=std::min<uint8_t>(content.count,content.networks.size());
  // The shared scanner may contain an earlier result, or another caller's
  // result. Publish only the scan this UI requested/joined; retain its terminal
  // failure until an explicit retry, while still updating live link facts.
  if(published.content && published.view==WifiView::List && published.request==WifiRequest::None &&
      (published.content->scan!=WifiScan::Scanning || observation.scan_sequence==scan_sequence_ || content.scan==WifiScan::Idle)){
    content.networks=published.content->networks;content.count=published.content->count;content.scan=published.content->scan;
    for(size_t i=0;i<content.count;++i)for(size_t n=0;n<std::min(observation.content.networks.size(),static_cast<size_t>(observation.content.count));++n)
      if(!std::strcmp(content.networks[i].ssid,observation.content.networks[n].ssid))content.networks[i].saved_index=observation.content.networks[n].saved_index;
  }
  if(!s->content || !equal(*s->content,content)){changed=true;s->content=std::make_shared<WifiContent>(content);}
  const WifiRequest request=s->request;
  if(request!=WifiRequest::None){
    s->request=WifiRequest::None;changed=true;started_=now;
    scan_sequence_=observation.scan_sequence;network_sequence_=observation.network_sequence;
    if(request==WifiRequest::Scan){
      connecting_=false;
      auto scanning=std::make_shared<WifiContent>(*s->content);scanning->scan=WifiScan::Scanning;s->content=scanning;
      if(!content.available || !port_.scan || !port_.scan(port_.context)){
        scanning->scan=WifiScan::Failed;std::snprintf(s->message,sizeof(s->message),"网络服务未就绪，请重试");
      }
    }else {
      connecting_=true;forgetting_=request==WifiRequest::Forget;
      if(!content.available || !content.editable || !port_.submit || !port_.submit(port_.context,s->selected,s->input.value,forgetting_,s->generation)){
        connecting_=false;fail(*s,"网络操作忙，请稍后重试");
        if(forgetting_){s->view=WifiView::Forget;s->focus=1;}
      }
    }
  }else if(s->view==WifiView::List && published.content && published.content->scan==WifiScan::Scanning){
    // The observation merge above already retains Scanning until this UI's
    // scanner publishes a new result. No repeated content clone is needed.
    if(static_cast<uint32_t>(now-started_)>=20000 && s->content->scan==WifiScan::Scanning){
      auto failed=std::make_shared<WifiContent>(*s->content);failed->scan=WifiScan::Failed;s->content=failed;std::snprintf(s->message,sizeof(s->message),"扫描超时，请重试");changed=true;
    }
    if(s->content->scan!=published.content->scan){s->focus=s->scroll=0;changed=true;}
  }
  if(connecting_ && s->view==WifiView::Connecting){
    const auto result=port_.submission?port_.submission(port_.context,generation_):WifiSubmission::ApplyFailed;
    if(result==WifiSubmission::Applied && forgetting_){
      s->view=WifiView::List;s->selected={};s->input.clear();s->focus=s->scroll=0;s->message[0]=0;
      s->request=WifiRequest::None;++s->generation;connecting_=false;changed=true;
    }else if(result==WifiSubmission::Applied||result==WifiSubmission::ConnectedUnsaved){
      const bool target=!std::strcmp(observation.target_ssid,s->selected.ssid);
      if(target && observation.link==WifiLink::Up && observation.network_sequence!=network_sequence_){
        s->view=WifiView::Connected;s->input.clear();s->focus=0;std::snprintf(s->message,sizeof(s->message),"%s",result==WifiSubmission::ConnectedUnsaved?"已连接，但未保存":"");
        for(size_t i=0;i<content.count;++i)if(!std::strcmp(content.networks[i].ssid,s->selected.ssid))s->selected.saved_index=content.networks[i].saved_index;
        connecting_=false;changed=true;
      }else if(target && observation.link!=WifiLink::Up && observation.authentication_failed && observation.failure_started_ms &&
          observation.network_sequence!=network_sequence_ && static_cast<uint32_t>(now-observation.failure_started_ms)<=static_cast<uint32_t>(now-started_)){
        fail(*s,"认证失败，请检查密码");connecting_=false;changed=true;
      }
    }else if(result!=WifiSubmission::Pending){
      fail(*s,result==WifiSubmission::SaveFailed?"凭据保存失败":result==WifiSubmission::Busy?"网络操作忙，请稍后重试":"连接请求失败，请重试");connecting_=false;changed=true;
      if(forgetting_){s->view=WifiView::Forget;s->focus=1;}
    }
    if(connecting_ && static_cast<uint32_t>(now-started_)>=30000){
      if(port_.abandon)port_.abandon(port_.context);
      fail(*s,"连接超时，请重试");connecting_=false;changed=true;
      if(forgetting_){s->view=WifiView::Forget;s->focus=1;}
    }
  }
  // A live link loss must not leave an unconditional "connected" detail page.
  if(s->view==WifiView::Connected && std::strcmp(content.connected_ssid,s->selected.ssid)){
    fail(*s,"网络已断开，请重试");changed=true;
  }
  changed|=text_input_tick(s->input,now);
  if(changed)publish_wifi_browser(shell,std::make_shared<WifiBrowserState>(std::move(draft)));
  // Repaint the waiting indicator on the existing snapshot at the bounded
  // observation cadence, without allocating another UI state for each frame.
  return reopened || changed || (shell.preferences.animations && wifi_browser(shell)->view==WifiView::Connecting);
}
} // namespace lunaredu::desktop
