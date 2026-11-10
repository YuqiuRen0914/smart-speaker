#include "lunaredu_music_backend.h"
namespace lunaredu::music {
namespace {
PlayerBackend backend;
bool unavailable(std::string &error) { error="music_backend_unavailable";return false; }
}
bool configure_player_backend(PlayerBackend value,std::string &error) {
  if(backend.snapshot){error="music_backend_already_configured";return false;}
  if(!value.snapshot||!value.refresh||!value.select||!value.action||!value.stop){error="music_backend_invalid";return false;}
  backend=value;error.clear();return true;
}
void configure_player(lunaros::KernelRuntime &) {}
PlaybackActivity music_activity(){
  if(backend.activity)return backend.activity(backend.context);
  const auto s=music_snapshot();return {s.audio_ready,s.playing,s.paused,s.stopping};
}
MusicSnapshot music_snapshot(){return backend.snapshot?backend.snapshot(backend.context):MusicSnapshot{};}
bool refresh_library(std::string &error){return backend.refresh?backend.refresh(backend.context,error):unavailable(error);}
bool select_track_snapshot(const MusicSnapshot &snapshot,size_t index,std::string &error){
  if(!backend.select)return unavailable(error);
  if(!snapshot.library||index>=snapshot.library->size()){error="music_track_invalid";return false;}
  return backend.select(backend.context,(*snapshot.library)[index],error);
}
bool select_track(size_t index,std::string &error){return select_track_snapshot(music_snapshot(),index,error);}
bool local_action(uint32_t id,std::string &error){return backend.action?backend.action(backend.context,id,error):unavailable(error);}
bool leave_local(std::string &error){if(backend.background_playback){error.clear();return true;}return stop_local(error);}
bool stop_local(std::string &error){return backend.stop?backend.stop(backend.context,error):unavailable(error);}
}
