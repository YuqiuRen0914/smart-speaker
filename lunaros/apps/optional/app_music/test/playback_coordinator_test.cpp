#include "lunaredu_music_playback_coordinator.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <vector>
#include <iostream>
using namespace lunaredu::music;
using CommandType=PlaybackCommandType;using Command=PlaybackCommand;using Event=PlaybackEvent;
using FilePlayerEvent=PlaybackEvent;
struct State:PlaybackState {std::vector<int> playlist;} g_runtime;
std::mutex state_mutex;
std::vector<Command> queued;std::vector<int> played;
std::atomic<uint32_t> g_visual_activity{0};
int storage_error=0,saved_mode=-1,pause_calls=0,resume_calls=0,pause_error=0,resume_error=0;
int stop_calls=0,stop_error=0;bool idle=true,muted=true,queue_ok=true,replace_on_enqueue=false;
uint32_t clock_ms=1000;
PlaybackPorts ports(){
  PlaybackPorts p;
  p.track_count=[](void *){return g_runtime.playlist.size();};
  p.now_ms=[](void *){return clock_ms;};p.entropy=[](void *){return 3u;};
  p.enqueue=[](void *,Command c){if(replace_on_enqueue)++g_runtime.playback_generation;if(queue_ok)queued.push_back(c);return queue_ok;};
  p.play_index=[](void *,int index){played.push_back(index);std::lock_guard<std::mutex> lock(state_mutex);
    g_runtime.current_index=index;g_runtime.playing=true;g_runtime.paused=false;g_runtime.first_frame_seen=true;
    g_runtime.auto_advance=true;g_runtime.advance_queued=false;++g_runtime.playback_generation;return true;};
  p.pause=[](void *){++pause_calls;return pause_error==0;};
  p.resume=[](void *){++resume_calls;return resume_error==0;};
  p.stop=[](void *){++stop_calls;return stop_error==0;};p.idle=[](void *){return idle;};
  p.discard_pending=[](void *){assert(!g_runtime.auto_advance&&!g_runtime.advance_queued);queued.clear();};
  p.fade_to_mute=[](void *,uint32_t ms){assert(ms==100);return muted;};p.fade_up=[](void *){};
  p.set_volume=[](void *,int){};p.refresh=[](void *){};
  p.persist_mode=[](void *,PlaybackMode mode){if(storage_error)return false;saved_mode=int(mode);return true;};
  return p;
}
PlaybackCoordinator coordinator{g_runtime,state_mutex,{70,true,3000,100},ports()};
void process_command(Command c){coordinator.execute(c);}
void reset(int count=3,int index=0,PlaybackMode mode=PlaybackMode::Sequential){
  g_runtime.playlist.resize(count);g_runtime.current_index=index;g_runtime.playback_mode=mode;
  g_runtime.playback_generation=1;g_runtime.playlist_revision=1;g_runtime.playing=true;g_runtime.paused=false;
  g_runtime.paused_by_volume=false;pause_calls=resume_calls=pause_error=resume_error=0;
  g_runtime.auto_advance=true;g_runtime.advance_queued=false;g_runtime.first_frame_seen=true;
  g_runtime.transfer_active=false;g_runtime.failed_tracks=0;g_runtime.mode_sequence=0;
  g_runtime.last_error.clear();queued.clear();played.clear();storage_error=0;saved_mode=-1;g_visual_activity=0;
}
void event(Event e){coordinator.on_event(e);}
void drain(){auto pending=queued;queued.clear();for(const auto& c:pending)process_command(c);}
int main(){
  for(int count=1;count<=30;++count)for(int current=0;current<count;++current)for(uint32_t random=0;random<90;++random){
    int next=advance_track(PlaybackMode::Shuffle,TrackAdvance::Finished,current,count,random);
    assert(next>=0 && next<count && (count==1 || next!=current));
    assert(advance_track(PlaybackMode::RepeatOne,TrackAdvance::Finished,current,count,random)==current);
  }
  assert(advance_track(PlaybackMode::Shuffle,TrackAdvance::Next,0,0,0)==-1);
  reset();event(FilePlayerEvent::Idle);assert(queued.size()==1);drain();assert(played.back()==1);
  reset(3,2);event(FilePlayerEvent::Idle);drain();assert(played.empty() && !g_runtime.playing && !g_runtime.auto_advance);
  reset(1,0,PlaybackMode::RepeatOne);event(FilePlayerEvent::Idle);drain();assert(played.back()==0);
  reset(1);event(FilePlayerEvent::Idle);drain();assert(played.empty());
  reset(3,0,PlaybackMode::RepeatOne);process_command({CommandType::kNext});assert(played.back()==1);
  process_command({CommandType::kPrevious});assert(played.back()==0);
  reset(3,0,PlaybackMode::RepeatOne);g_runtime.first_frame_seen=false;
  event(FilePlayerEvent::Failed);event(FilePlayerEvent::Idle);
  assert(queued.size()==1);drain();assert(played.back()==1);
  g_runtime.first_frame_seen=false;event(FilePlayerEvent::Idle);drain();assert(played.back()==2);
  g_runtime.first_frame_seen=false;event(FilePlayerEvent::Idle);drain();
  assert(played.size()==2 && !g_runtime.auto_advance && g_runtime.last_error=="playlist_unplayable");
  reset();event(FilePlayerEvent::Idle);process_command({CommandType::kStop});drain();assert(played.empty());
  reset();event(FilePlayerEvent::Idle);g_runtime.paused=true;drain();assert(played.empty());
  reset();event(FilePlayerEvent::Idle);process_command({CommandType::kPlayTrack,2,1});drain();
  assert(played.size()==1 && played.back()==2);
  reset();g_runtime.transfer_active=true;event(FilePlayerEvent::Idle);assert(queued.empty());
  reset();for(int value : {1,2,0}){
    process_command({CommandType::kCycleMode});assert(int(g_runtime.playback_mode)==value && saved_mode==value);
    assert(g_runtime.playing && played.empty());
  }
  assert(g_runtime.mode_sequence==3);
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
  event(FilePlayerEvent::Paused); // Decoder acknowledgement preserves the reason.
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
  reset();queue_ok=false;event(Event::Idle);
  assert(!g_runtime.advance_queued&&g_runtime.last_error=="track_advance_queue_full");
  reset();replace_on_enqueue=true;event(Event::Idle);
  assert(g_runtime.last_error.empty());replace_on_enqueue=false;queue_ok=true;
  reset();g_runtime.first_frame_seen=false;g_runtime.play_started_ms=UINT32_MAX-2000;clock_ms=1000;
  coordinator.tick();assert(queued.size()==1&&queued[0].type==CommandType::kSkipFailed);
  coordinator.tick();assert(queued.size()==1);
  reset();coordinator.first_frame();clock_ms=10000;coordinator.tick();assert(queued.empty());
  reset(1);g_runtime.first_frame_seen=false;coordinator.tick();assert(queued.empty());
  reset();process_command({CommandType::kAdjustVolume,INT_MAX});assert(g_runtime.volume==70);
  process_command({CommandType::kAdjustVolume,INT_MIN});assert(g_runtime.volume==0);
  std::string error;idle=false;stop_error=1;
  assert(!coordinator.stop_checked(error)&&error=="music_stop_failed");
  stop_error=0;stop_calls=0;
  assert(!coordinator.stop_checked(error)&&error=="music_stop_pending"&&stop_calls==1);
  assert(!coordinator.stop_checked(error)&&stop_calls==1);
  idle=true;muted=false;assert(!coordinator.stop_checked(error)&&error=="music_output_stop_pending");
  muted=true;assert(coordinator.stop_checked(error)&&!g_runtime.playing&&!g_runtime.auto_advance);
  assert(coordinator.stop_checked(error)&&stop_calls==1);
  coordinator.begin_track_locked(0,clock_ms);idle=false;
  assert(!coordinator.stop_checked(error)&&stop_calls==2);idle=true;assert(coordinator.stop_checked(error));
  State other;other.playing=true;other.volume=25;int previous_pauses=pause_calls;
  PlaybackCoordinator basic{other,state_mutex,{30,false,0,100},ports()};
  basic.execute({CommandType::kSetVolume,0});assert(other.volume==0&&other.playing&&pause_calls==previous_pauses);
  basic.execute({CommandType::kSetVolume,100});assert(other.volume==30);
  std::cout<<"Shared Music coordinator: modes, completion fences, transfer/queue failures, pause reasons, volume policy, first-frame timeout/wrap and checked retirement passed\n";

}
