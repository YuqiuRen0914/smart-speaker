#pragma once
#include "lunaredu_music_presentation_snapshot.h"
#include "lunaredu_music_actions.h"
#include "desktop/composition/selection.h"
#include "desktop/ui/paint_port.h"
#include <algorithm>
namespace lunaredu::music {
inline constexpr const char *kApplicationId="music";
inline constexpr app::ViewRoute kView{kApplicationId,0,"music",true};
struct Presentation {
  std::shared_ptr<const MusicSnapshot> model;
  unsigned scroll=0;
  void paint(desktop::UiPaintPort &,bool)const;
};
static_assert(sizeof(Presentation)<=64,"Music frames retain a compact immutable snapshot");
class Application {
 public:
  bool declare(app::AppController &c,lunaros::DesktopApplicationPorts p,std::string &e);
 private:app::HostedApplication owner_;lunaros::DesktopApplicationPorts ports_;
};
struct Registration{bool operator()(Application &a,app::AppController &c,lunaros::DesktopApplicationPorts p,std::string &e)const{return a.declare(c,p,e);}};
template<class Host>class Session:public lunaros::DesktopSessionDefaults {
 public:
  explicit Session(Host &h):host(h){}
  bool widgets()const{return host.state().page==kView;}
  void poll(bool allowed){
    if(!allowed||host.state().page!=kView)return;
    const auto now=Host::Configuration::now_ms();if(now-last<200)return;last=now;
    if(!loaded){loaded=true;(void)refresh_library(input_error);}
    const auto snapshot=snapshots.capture(input_error);
    if(snapshot!=model||host.take_redraw()){model=snapshot;host.publish_application(host.model(),Host::Paint::Full);}
  }
  template<class Base>struct Context:Base{
    Context(Base b,Session &s):Base(std::move(b)),session(s){}
    Presentation music_presentation()const{return {session.snapshots.capture(session.input_error),session.scroll};}
    bool music_input(){
      if(this->system_owned())return false;
      if(this->back||this->edge_back){this->show_shell();return true;}
      lunaros::graphics::Action a;
      while(this->host.ui().take_action(a)){
        if(a.id==233)session.scroll=unsigned(std::clamp(a.value,int32_t(0),int32_t(8192)));
        else if(a.id==225){this->host.ui().enter_ui({desktop::kShellView,desktop::UiEntryKind::Menu,"os.settings"});continue;}
        else {const auto state=session.model?*session.model:music_snapshot();(void)submit_action(a.id,state,session.input_error);}
        this->host.publish_application(this->model,Host::Paint::Full);
      }
      return true;
    }
    Session &session;
  };
 private:Host &host;PresentationSnapshotCache snapshots;std::shared_ptr<const MusicSnapshot> model;
  unsigned scroll=0;uint32_t last=0;std::string input_error;bool loaded=false;
};
struct DesktopEntry:lunaros::DesktopEntryDefaults{
  static constexpr size_t home_widget_count=1;
  static const desktop::HomeWidgetDefinition *home_widgets(){
    static const desktop::HomeWidgetDefinition widget={"music.playback",kApplicationId,"音乐",2,1,[]{
      const auto s=music_snapshot();desktop::HomeWidgetContent value;
      std::snprintf(value.value,sizeof(value.value),"%s",s.speaker.receiving?(s.speaker.playing?"蓝牙播放":"等待手机播放"):s.playing?(s.paused?"本地已暂停":"本地播放"):"本地音乐");
      const char *title=s.library&&s.selected<s.library->size()?(*s.library)[s.selected].title.c_str():"点击打开曲库";
      std::snprintf(value.detail,sizeof(value.detail),"%s",title);value.color=0x05ca;return value;
    },1000};return &widget;
  }
  using Presentation=music::Presentation;template<class Host>using Session=music::Session<Host>;
  static auto tile(){return desktop::DesktopApplication{kApplicationId,"音乐",desktop::DesktopIcon::Music,0x05ca};}
  static auto provider(){return lunaros::DesktopAppDescriptor{kApplicationId,lunaros::DesktopAppOwner<Application,Registration>::create};}
  template<class Bindings>static typename Bindings::Definition binding(){return {kApplicationId,
    [](typename Bindings::CaptureContext &c)->typename Bindings::Presentation{return c.music_presentation();},
    [](typename Bindings::InputContext &c){return !c.primary&&c.music_input();},
    [](typename Bindings::EntryContext &c){c.enter_ui({kView});},
    [](typename Bindings::LaunchContext &c,std::string &e){return c.host.applications().switch_to(kApplicationId,e)&&c.enter_ui({kView});}};
  }
};
}
