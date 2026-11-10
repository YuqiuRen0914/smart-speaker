#include "lunaredu_music_backend.h"
#include "lunaredu_music_desktop.h"
#include "kernel/runtime.h"
#include "app/execution/application_context.h"
#include "app/lifecycle/app_controller.h"
#include <cassert>
#include <iostream>
using namespace lunaredu;
namespace {
struct Backend {
  music::MusicSnapshot state;
  bool retired=false;
  std::string selected;
  unsigned actions=0,stops=0,snapshots=0,activities=0;
};
}
int main(int argc,char **){
  const bool background=argc>1;
  std::string error;
  assert(!music::refresh_library(error)&&error=="music_backend_unavailable");
  assert(!music::configure_player_backend({},error));
  Backend backend;
  backend.state.library=std::make_shared<const music::Library>(music::Library{{"/sdcard/music/a.mp3","a",true}});
  backend.state.lyric="current lyric";backend.state.playback_mode="顺序播放";
  assert(music::configure_player_backend({&backend,
    [](void *p){auto &b=*static_cast<Backend *>(p);++b.snapshots;return b.state;},
    [](void *,std::string &e){e.clear();return true;},
    [](void *p,const music::Track &track,std::string &e){static_cast<Backend *>(p)->selected=track.path;e.clear();return true;},
    [](void *p,uint32_t,std::string &e){++static_cast<Backend *>(p)->actions;e.clear();return true;},
    [](void *p,std::string &e){auto &b=*static_cast<Backend *>(p);++b.stops;if(!b.retired){e="music_stop_pending";return false;}e.clear();return true;},background,
    [](void *p){auto &b=*static_cast<Backend *>(p);++b.activities;return music::PlaybackActivity{b.state.audio_ready,b.state.playing,b.state.paused,b.state.stopping};}
  },error));
  assert(!music::configure_player_backend({},error)&&error=="music_backend_already_configured");
  backend.state.audio_ready=true;backend.state.playing=true;
  for(unsigned i=0;i<1000;++i){const auto activity=music::music_activity();assert(activity.audio_ready&&activity.playing&&!activity.paused&&!activity.stopping);}
  assert(backend.activities==1000&&backend.snapshots==0);
  const auto captured=music::music_snapshot();
  assert(captured.library==backend.state.library&&captured.lyric=="current lyric");
  assert(music::refresh_library(error)&&music::select_track(0,error)&&backend.selected=="/sdcard/music/a.mp3");
  assert(!music::select_track(3,error)&&error=="music_track_invalid");
  backend.state.library=std::make_shared<const music::Library>(music::Library{{"/sdcard/music/b.mp3","b",true}});
  assert(music::select_track_snapshot(captured,0,error)&&backend.selected=="/sdcard/music/a.mp3");
  assert(music::local_action(218,error)&&backend.actions==1);
  assert(!music::stop_local(error)&&backend.stops==1);

  lunaros::KernelRuntime runtime;assert(runtime.initialize(error));
  lunaros::ApplicationContext context(runtime);app::AppController controller(context);
  app::HostedApplication shell;app::ForegroundManifest manifest;manifest.title="Shell";
  assert(shell.declare(controller,"test.shell",manifest,{},nullptr,error));
  music::Application application;
  assert(application.declare(controller,{},error));
  assert(controller.setup(music::kApplicationId,error));
  assert(controller.resource_owner_holds("app:music","display.main"));
  if(background){
    backend.state.playing=true;
    const auto stops=backend.stops;
    assert(controller.switch_to("test.shell",error));
    assert(backend.state.playing&&backend.stops==stops);
    assert(!controller.resource_owner_holds("app:music","display.main"));
    assert(controller.switch_to(music::kApplicationId,error));
    assert(backend.state.playing&&backend.stops==stops);
    assert(!music::stop_local(error));
    backend.retired=true;assert(music::stop_local(error));
    assert(controller.stop("test_complete"));
    std::cout<<"PASS service-owned Music keeps playback across foreground switches; explicit maintenance stop remains checked\n";
    return 0;
  }
  assert(!controller.switch_to("test.shell",error));
  assert(controller.resource_owner_holds("app:music","display.main"));
  assert(!controller.foreground_current("test.shell"));
  backend.retired=true;
  assert(controller.switch_to("test.shell",error));
  assert(controller.foreground_current("test.shell"));
  assert(!controller.resource_owner_holds("app:music","display.main"));
  assert(controller.stop("test_complete"));
  std::cout<<"External Music backend: one binding, retained library, commands and checked foreground retirement passed\n";
}
