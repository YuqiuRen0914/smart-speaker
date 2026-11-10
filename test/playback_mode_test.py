"""Exercise production completion/command/mode routines with audio/NVS doubles."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

app = Path(__file__).resolve().parents[1]
declarations = (app/'main/smart_speaker/runtime_internal.h').read_text(encoding='utf-8')
source = (app/'main/smart_speaker/smart_speaker_runtime.cpp').read_text(encoding='utf-8')
def declaration(name):
    return re.search(r'(?:enum class|struct) '+name+r'\b.*?\n\};',declarations,re.S).group()
completion = source[source.index('void queue_track_completion('):source.index('void IRAM_ATTR encoder_isr(')]
play_state = source[source.index('void update_play_state('):source.index('bool play_index(')]
code = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <mutex>
#include <memory>
#include "smart_speaker_lyrics.h"
#include <string>
#include <vector>
#include "smart_speaker_playback_mode.h"
#include "smart_speaker_character.h"
using namespace smart_speaker;
#define ESP_LOGI(...) ((void)0)
constexpr int kInitialVolume=10, ESP_OK=0, NVS_READWRITE=1;
using esp_err_t=int;using nvs_handle_t=int;
int storage_error=0,saved_mode=-1;
int nvs_open(const char*,int,int* h){*h=1;return storage_error;}
int nvs_set_u8(int,const char*,uint8_t v){saved_mode=v;return storage_error;}
int nvs_commit(int){return storage_error;}void nvs_close(int){}
uint32_t now_ms(){return 1000;} uint32_t esp_random(){return 3;}
struct TrackInfo {std::string path;};
''' + declaration('CommandType') + declaration('Command') + declaration('Runtime') + r'''
Runtime g_runtime;std::mutex g_player_operation_mutex;
std::atomic<uint32_t> g_visual_activity{0};
std::vector<Command> queued;
std::vector<int> played;
bool enqueue_command(CommandType t,int v=0,uint32_t r=0){queued.push_back({t,v,r});return true;}
void set_error_locked(const char* e){g_runtime.last_error=e;}
''' + play_state + r'''
bool play_index(int index){played.push_back(index);g_runtime.current_index=index;g_runtime.playing=true;g_runtime.paused=false;
  g_runtime.first_frame_seen=true;g_runtime.auto_advance=true;g_runtime.advance_queued=false;++g_runtime.playback_generation;return true;}
int dual_i2s_audio_fade_to_mute(int){return 0;}void dual_i2s_audio_fade_up(){}
int pause_calls=0,resume_calls=0,pause_error=0,resume_error=0;
int audio_player_resume(){++resume_calls;return resume_error;}int audio_player_pause(){++pause_calls;return pause_error;}int audio_player_stop(){return 0;}
int clamp_volume(int x){return std::clamp(x,0,70);}void dual_i2s_audio_set_volume(int){}void rescan_locked(){}
enum Event {AUDIO_PLAYER_CALLBACK_EVENT_PLAYING,AUDIO_PLAYER_CALLBACK_EVENT_COMPLETED_PLAYING_NEXT,
 AUDIO_PLAYER_CALLBACK_EVENT_PAUSE,AUDIO_PLAYER_CALLBACK_EVENT_IDLE,AUDIO_PLAYER_CALLBACK_EVENT_UNKNOWN_FILE_TYPE};
struct audio_player_cb_ctx_t {Event audio_event;};
''' + completion + r'''
void reset(int count=3,int index=0,PlaybackMode mode=PlaybackMode::Sequential){
  g_runtime.playlist.resize(count);g_runtime.current_index=index;g_runtime.playback_mode=mode;
  g_runtime.playback_generation=1;g_runtime.playlist_revision=1;g_runtime.playing=true;g_runtime.paused=false;
  g_runtime.paused_by_volume=false;pause_calls=resume_calls=pause_error=resume_error=0;
  g_runtime.auto_advance=true;g_runtime.advance_queued=false;g_runtime.first_frame_seen=true;
  g_runtime.uploading=false;g_runtime.failed_tracks=0;g_runtime.mode_sequence=0;
  g_runtime.last_error.clear();queued.clear();played.clear();storage_error=0;saved_mode=-1;g_visual_activity=0;
}
void event(Event e){audio_player_cb_ctx_t context{e};handle_audio_event(&context);}
void drain(){auto pending=queued;queued.clear();for(const auto& c:pending)process_command(c);}
int main(){
  for(int count=1;count<=30;++count)for(int current=0;current<count;++current)for(uint32_t random=0;random<90;++random){
    int next=advance_track(PlaybackMode::Shuffle,TrackAdvance::Finished,current,count,random);
    assert(next>=0 && next<count && (count==1 || next!=current));
    assert(advance_track(PlaybackMode::RepeatOne,TrackAdvance::Finished,current,count,random)==current);
  }
  assert(advance_track(PlaybackMode::Shuffle,TrackAdvance::Next,0,0,0)==-1);
  reset();event(AUDIO_PLAYER_CALLBACK_EVENT_IDLE);assert(queued.size()==1);drain();assert(played.back()==1);
  reset(3,2);event(AUDIO_PLAYER_CALLBACK_EVENT_IDLE);drain();assert(played.empty() && !g_runtime.playing && !g_runtime.auto_advance);
  reset(1,0,PlaybackMode::RepeatOne);event(AUDIO_PLAYER_CALLBACK_EVENT_IDLE);drain();assert(played.back()==0);
  reset(1);event(AUDIO_PLAYER_CALLBACK_EVENT_IDLE);drain();assert(played.empty());
  reset(3,0,PlaybackMode::RepeatOne);process_command({CommandType::kNext});assert(played.back()==1);
  process_command({CommandType::kPrevious});assert(played.back()==0);
  reset(3,0,PlaybackMode::RepeatOne);g_runtime.first_frame_seen=false;
  event(AUDIO_PLAYER_CALLBACK_EVENT_UNKNOWN_FILE_TYPE);event(AUDIO_PLAYER_CALLBACK_EVENT_IDLE);
  assert(queued.size()==1);drain();assert(played.back()==1);
  g_runtime.first_frame_seen=false;event(AUDIO_PLAYER_CALLBACK_EVENT_IDLE);drain();assert(played.back()==2);
  g_runtime.first_frame_seen=false;event(AUDIO_PLAYER_CALLBACK_EVENT_IDLE);drain();
  assert(played.size()==2 && !g_runtime.auto_advance && g_runtime.last_error=="playlist_unplayable");
  reset();event(AUDIO_PLAYER_CALLBACK_EVENT_IDLE);process_command({CommandType::kStop});drain();assert(played.empty());
  reset();event(AUDIO_PLAYER_CALLBACK_EVENT_IDLE);g_runtime.paused=true;drain();assert(played.empty());
  reset();event(AUDIO_PLAYER_CALLBACK_EVENT_IDLE);process_command({CommandType::kPlayTrack,2,1});drain();
  assert(played.size()==1 && played.back()==2);
  reset();g_runtime.uploading=true;event(AUDIO_PLAYER_CALLBACK_EVENT_IDLE);assert(queued.empty());
  reset();for(int value : {1,2,0}){
    process_command({CommandType::kCycleMode});assert(int(g_runtime.playback_mode)==value && saved_mode==value);
    assert(g_runtime.playing && played.empty());
  }
  assert(g_runtime.mode_sequence==3 && (g_visual_activity.load()&7)==uint32_t(character::Action::Mode));
  reset();g_runtime.volume=1;pause_calls=0;
  process_command({CommandType::kAdjustVolume,-1});
  assert(g_runtime.volume==0 && g_runtime.paused && !g_runtime.playing && pause_calls==1);
  process_command({CommandType::kAdjustVolume,2});
  assert(g_runtime.volume==2 && g_runtime.playing && !g_runtime.paused && pause_calls==1 && resume_calls==1);
  assert(!g_runtime.paused_by_volume && played.empty()); // Resume the same file/position.
  process_command({CommandType::kSetVolume,0});assert(pause_calls==2);
  reset();g_runtime.volume=10;pause_calls=0;
  process_command({CommandType::kSetVolume,0});
  assert(g_runtime.paused && g_runtime.paused_by_volume && pause_calls==1);
  event(AUDIO_PLAYER_CALLBACK_EVENT_PAUSE); // Decoder acknowledgement preserves the reason.
  assert(g_runtime.paused_by_volume);
  process_command({CommandType::kSetVolume,3});
  assert(g_runtime.playing && !g_runtime.paused_by_volume && resume_calls==1);
  // A manual pause before or after muting must never be undone by volume.
  for(bool pause_first : {false,true}) {
    reset();g_runtime.volume=10;
    if(pause_first)process_command({CommandType::kPause});
    process_command({CommandType::kSetVolume,0});
    if(!pause_first)process_command({CommandType::kPause});
    process_command({CommandType::kSetVolume,10});
    assert(g_runtime.paused && !g_runtime.paused_by_volume && resume_calls==0);
  }
  reset();g_runtime.volume=10;process_command({CommandType::kSetVolume,0});
  process_command({CommandType::kStop});process_command({CommandType::kSetVolume,10});
  assert(!g_runtime.playing && !g_runtime.paused_by_volume && resume_calls==0);
  reset();g_runtime.volume=10;process_command({CommandType::kSetVolume,0});
  process_command({CommandType::kNext});process_command({CommandType::kSetVolume,10});
  assert(g_runtime.playing && !g_runtime.paused_by_volume && resume_calls==0 && played.back()==1);
  reset();g_runtime.volume=10;pause_error=1;
  process_command({CommandType::kSetVolume,0});process_command({CommandType::kSetVolume,10});
  assert(!g_runtime.paused_by_volume && resume_calls==0 && g_runtime.last_error=="playback_pause_failed");
  reset();g_runtime.volume=10;process_command({CommandType::kSetVolume,0});resume_error=1;
  process_command({CommandType::kSetVolume,2});
  assert(g_runtime.paused && g_runtime.paused_by_volume && g_runtime.last_error=="playback_resume_failed");
  resume_error=0;process_command({CommandType::kAdjustVolume,1});
  assert(g_runtime.playing && !g_runtime.paused_by_volume && resume_calls==2);
  reset();g_runtime.playing=false;g_runtime.volume=10;pause_calls=0;
  process_command({CommandType::kSetVolume,0});process_command({CommandType::kSetVolume,10});
  assert(pause_calls==0 && resume_calls==0 && !g_runtime.playing);
  reset();g_runtime.playback_mode=PlaybackMode::Sequential;g_runtime.mode_sequence=3;
  storage_error=1;process_command({CommandType::kCycleMode});
  assert(g_runtime.playback_mode==PlaybackMode::Sequential && g_runtime.mode_sequence==3);
  assert(g_runtime.last_error=="playback_mode_save_failed");
}
'''
with tempfile.TemporaryDirectory(prefix='speaker-mode-') as temp:
    path=Path(temp);test=path/'test.cpp';binary=path/'test.exe'
    test.write_text(code,encoding='utf-8')
    subprocess.run([os.environ.get('CXX', 'g++'),'-std=c++17','-D_WIN32_WINNT=0x0601','-include',str(app/'lunaros_apps/lunaredu_pet/test/host/mutex.h'),'-Wall','-Wextra','-Werror','-I',str(app/'main/smart_speaker'),str(test),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('Playback modes: sequential EOF, shuffle exclusion, single repeat, manual skip, bad-file bounds, stale EOF and persistence failure passed')
