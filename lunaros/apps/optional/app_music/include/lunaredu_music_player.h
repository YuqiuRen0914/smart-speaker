#pragma once
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <cstdint>
namespace lunaredu::lunaros {class KernelRuntime;}
namespace lunaredu::music {
struct Track {
  std::string path,title;
  bool mp3=false;
};
using Library=std::vector<Track>;
struct SpeakerSnapshot {
  bool supported=false,enabled=false,receiving=false,connected=false,playing=false,controllable=false;
  std::array<char,96> error{};
  bool operator==(const SpeakerSnapshot &o)const {
    return supported==o.supported&&enabled==o.enabled&&receiving==o.receiving&&connected==o.connected&&
      playing==o.playing&&controllable==o.controllable&&error==o.error;
  }
};
SpeakerSnapshot speaker_snapshot();
bool speaker_action(uint32_t id,const SpeakerSnapshot &,std::string &);
struct MusicSnapshot {
  std::shared_ptr<const Library> library;
  size_t selected=0;
  bool storage_ready=false,audio_ready=false,playing=false,paused=false,stopping=false;
  uint32_t position_seconds=0;
  uint8_t volume=0;
  std::string lyric,playback_mode,library_hint;
  SpeakerSnapshot speaker;
  std::array<char,96> error{};
  bool operator==(const MusicSnapshot &o)const {
    return library==o.library&&selected==o.selected&&storage_ready==o.storage_ready&&audio_ready==o.audio_ready&&
      playing==o.playing&&paused==o.paused&&stopping==o.stopping&&position_seconds==o.position_seconds&&
      volume==o.volume&&lyric==o.lyric&&playback_mode==o.playback_mode&&library_hint==o.library_hint&&speaker==o.speaker&&error==o.error;
  }
  bool operator!=(const MusicSnapshot &o)const {return !(*this==o);}
};
// Cheap observation for animation and health loops; no library/string/DSP copies.
struct PlaybackActivity {bool audio_ready=false,playing=false,paused=false,stopping=false;};
PlaybackActivity music_activity();
void configure_player(lunaros::KernelRuntime &);
MusicSnapshot music_snapshot();
bool refresh_library(std::string &);
bool select_track(size_t,std::string &);
bool select_track_snapshot(const MusicSnapshot &,size_t,std::string &);
bool local_action(uint32_t id,std::string &);
bool stop_local(std::string &);
bool leave_local(std::string &);
}
