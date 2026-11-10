#include "lunaredu_music_playback_coordinator.h"
#include <algorithm>
namespace lunaredu::music {
PlaybackCoordinator::PlaybackCoordinator(PlaybackState &state,std::mutex &mutex,
    PlaybackPolicy policy,PlaybackPorts ports):state_(state),mutex_(mutex),policy_(policy),ports_(ports){}
void PlaybackCoordinator::error_locked(const char *error){
  state_.last_error=error?error:"";
  if(ports_.error&&!state_.last_error.empty())ports_.error(ports_.context,state_.last_error.c_str());
}
void PlaybackCoordinator::update_state(bool playing,bool paused,bool automatic){
  std::lock_guard<std::mutex> lock(mutex_);
  state_.playing=playing;state_.paused=paused;state_.auto_advance=automatic;
  if(!paused)state_.paused_by_volume=false;
}
void PlaybackCoordinator::first_frame(){state_.first_frame_seen=true;state_.decode_timeout_skip_sent=false;}
void PlaybackCoordinator::begin_track_locked(int index,uint32_t now){
  stop_requested_=false;
  state_.current_index=index;state_.paused_by_volume=false;state_.playing=true;state_.paused=false;
  state_.auto_advance=true;state_.first_frame_seen=false;state_.decode_timeout_skip_sent=false;
  state_.play_started_ms=now;++state_.playback_generation;state_.advance_queued=false;
  state_.last_error.clear();
}
void PlaybackCoordinator::complete(bool failed,bool idle_event){
  uint32_t generation=0;bool queued=false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if(state_.auto_advance&&state_.playing&&!state_.transfer_active&&!state_.advance_queued&&ports_.track_count(ports_.context)){
      failed=failed||!state_.first_frame_seen;state_.advance_queued=true;
      generation=state_.playback_generation;queued=true;
      state_.playing=false;state_.paused=false;state_.paused_by_volume=false;
    }else if(idle_event){state_.playing=false;state_.paused=false;state_.paused_by_volume=false;}
  }
  // Completion only queues work; it must never reenter a decoder callback.
  if(queued&&!ports_.enqueue(ports_.context,{failed?PlaybackCommandType::kSkipFailed:PlaybackCommandType::kAutoNext,0,generation})){
    std::lock_guard<std::mutex> lock(mutex_);
    if(state_.playback_generation==generation){state_.advance_queued=false;error_locked("track_advance_queue_full");}
  }
}
void PlaybackCoordinator::on_event(PlaybackEvent event){
  switch(event){
    case PlaybackEvent::Playing:update_state(true,false,true);break;
    case PlaybackEvent::Paused:update_state(false,true,true);break;
    case PlaybackEvent::Idle:complete(false,true);break;
    case PlaybackEvent::Failed:complete(true);break;
  }
}
void PlaybackCoordinator::cycle_mode(){
  PlaybackMode mode;
  {std::lock_guard<std::mutex> lock(mutex_);mode=next_playback_mode(state_.playback_mode);}
  const bool saved=ports_.persist_mode(ports_.context,mode);
  std::lock_guard<std::mutex> lock(mutex_);
  if(!saved){error_locked("playback_mode_save_failed");return;}
  state_.playback_mode=mode;++state_.mode_sequence;state_.mode_updated_ms=ports_.now_ms(ports_.context);
}
void PlaybackCoordinator::tick(){
  bool timeout=false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if(policy_.first_frame_timeout_ms&&state_.playing&&!state_.paused&&!state_.first_frame_seen&&
        !state_.decode_timeout_skip_sent&&ports_.track_count(ports_.context)>1&&
        ports_.now_ms(ports_.context)-state_.play_started_ms>=policy_.first_frame_timeout_ms){
      state_.decode_timeout_skip_sent=true;timeout=true;error_locked("track_decode_timeout");
    }
  }
  if(timeout)complete(true);
}
bool PlaybackCoordinator::stop_checked(std::string &error){
  {std::lock_guard<std::mutex> lock(mutex_);state_.auto_advance=false;state_.advance_queued=false;}
  if(ports_.discard_pending)ports_.discard_pending(ports_.context);
  if(!ports_.idle(ports_.context)){
    if(!stop_requested_){if(!ports_.stop(ports_.context)){error="music_stop_failed";return false;}stop_requested_=true;}
    error="music_stop_pending";return false;
  }
  if(!ports_.fade_to_mute(ports_.context,policy_.fade_timeout_ms)){error="music_output_stop_pending";return false;}
  stop_requested_=false;update_state(false,false,false);error.clear();return true;
}
void PlaybackCoordinator::execute(const PlaybackCommand &command) {
  if (command.type == PlaybackCommandType::kCycleMode) { cycle_mode(); return; }
  // Explicit transport commands override a pause caused by muting, including
  // a second manual pause while already muted. Volume alone may resume only
  // the playback it paused; it must never start an idle or stopped player.
  if (command.type == PlaybackCommandType::kPlay || command.type == PlaybackCommandType::kPlayTrack ||
      command.type == PlaybackCommandType::kPause || command.type == PlaybackCommandType::kStop ||
      command.type == PlaybackCommandType::kNext || command.type == PlaybackCommandType::kPrevious ||
      command.type == PlaybackCommandType::kRescan) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.paused_by_volume = false;
  }
  switch (command.type) {
  case PlaybackCommandType::kPlayTrack: {
    int index = -1;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      // A rescan between admission and execution must never select a different song.
      state_.failed_tracks = 0;
      if (command.playlist_revision == state_.playlist_revision &&
          command.value >= 0 && static_cast<size_t>(command.value) < ports_.track_count(ports_.context))
        index = command.value;
      if (index < 0) error_locked("track_not_found");
    }
    if (index >= 0) (void)ports_.play_index(ports_.context,index);
    break;
  }
  case PlaybackCommandType::kPlay: {
    bool paused = false;
    int current_index = -1;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      paused = state_.paused;
      state_.failed_tracks = 0;
      current_index = state_.current_index;
    }
    if (paused) {
      if (ports_.resume(ports_.context)) {
        update_state(true, false, true);
        ports_.fade_up(ports_.context);
      }
      return;
    }
    (void)ports_.play_index(ports_.context,current_index);
    break;
  }
  case PlaybackCommandType::kPause:
    (void)ports_.fade_to_mute(ports_.context,policy_.fade_timeout_ms);
    if (ports_.pause(ports_.context)) update_state(false, true, true);
    break;
  case PlaybackCommandType::kStop:
    (void)ports_.fade_to_mute(ports_.context,policy_.fade_timeout_ms);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      state_.auto_advance = false;
    }
    (void)ports_.stop(ports_.context);
    update_state(false, false, false);
    break;
  case PlaybackCommandType::kNext:
  case PlaybackCommandType::kAutoNext:
  case PlaybackCommandType::kSkipFailed: {
    int next_index = -1;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const bool automatic = command.type != PlaybackCommandType::kNext;
      if (automatic && (!state_.auto_advance || !state_.advance_queued || state_.transfer_active || state_.paused ||
                        command.playlist_revision != state_.playback_generation)) break;
      state_.advance_queued = false;
      const bool failed = command.type == PlaybackCommandType::kSkipFailed;
      state_.failed_tracks = failed ? state_.failed_tracks + 1 : 0;
      if (failed && state_.failed_tracks >= ports_.track_count(ports_.context)) {
        state_.auto_advance = false;
        error_locked("playlist_unplayable");
        break;
      }
      next_index = advance_track(state_.playback_mode, failed ? TrackAdvance::Failed :
          automatic ? TrackAdvance::Finished : TrackAdvance::Next, state_.current_index,
          static_cast<int>(ports_.track_count(ports_.context)), ports_.entropy(ports_.context));
      if (automatic && next_index < 0) state_.auto_advance = false;
    }
    if (next_index >= 0) (void)ports_.play_index(ports_.context,next_index);
    break;
  }
  case PlaybackCommandType::kPrevious: {
    int previous_index = -1;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      state_.failed_tracks = 0;
      if (ports_.track_count(ports_.context)!=0) {
        const int count = static_cast<int>(ports_.track_count(ports_.context));
        const int current =
            state_.current_index < 0 ? 0 : state_.current_index;
        previous_index = (current - 1 + count) % count;
      }
    }
    if (previous_index >= 0) (void)ports_.play_index(ports_.context,previous_index);
    break;
  }
  case PlaybackCommandType::kSetVolume:
  case PlaybackCommandType::kAdjustVolume: {
    int volume = 0;
    bool pause_at_zero = false;
    bool resume_from_zero = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const int previous = state_.volume;
      const int64_t desired=command.type==PlaybackCommandType::kSetVolume?int64_t(command.value):int64_t(previous)+command.value;
      volume=int(std::clamp<int64_t>(desired,0,std::max(0,policy_.maximum_volume)));
      state_.volume = volume;
      pause_at_zero = policy_.pause_at_zero && volume == 0 && previous > 0 && state_.playing;
      resume_from_zero = policy_.pause_at_zero && volume > 0 && state_.paused && state_.paused_by_volume;
    }
    ports_.set_volume(ports_.context,volume);
    if (pause_at_zero) {
      (void)ports_.fade_to_mute(ports_.context,policy_.fade_timeout_ms);
      if (ports_.pause(ports_.context)) {
        update_state(false, true, true);
        std::lock_guard<std::mutex> lock(mutex_);
        state_.paused_by_volume = true;
      }
      else { std::lock_guard<std::mutex> lock(mutex_); error_locked("playback_pause_failed"); }
    } else if (resume_from_zero) {
      if (ports_.resume(ports_.context)) {
        update_state(true, false, true);
        ports_.fade_up(ports_.context);
      } else {
        std::lock_guard<std::mutex> lock(mutex_);
        error_locked("playback_resume_failed");
      }
    }
    break;
  }
  case PlaybackCommandType::kRescan:
    ports_.refresh(ports_.context);
    break;
  case PlaybackCommandType::kCycleMode: break; // Handled before the player lock.
  }
}

}
