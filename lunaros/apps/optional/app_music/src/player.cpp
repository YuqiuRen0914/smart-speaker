#include "lunaredu_music_player.h"
#include "lunaredu_music_backend.h"
#include "lunaros_storage.h"
#include "lunaros_audio_bridge.h"
#include "lunaros_audio_volume.h"
#include "lunaros_audio_file_playback.h"
#include "lunaros_audio_library.h"
#include "lunaros_audio_focus.h"
#include "kernel/runtime.h"
#include "kernel/managed_task.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <mutex>
#include <new>

namespace lunaredu::music {
bool configure_player_backend(PlayerBackend,std::string &error) {
  error="music_backend_requires_external_player";return false;
}
namespace {
constexpr const char *kDirectory="/performances/music";
constexpr size_t kMaximumTracks=64;
std::mutex mutex;
MusicSnapshot state;
lunaros::KernelRuntime *runtime=nullptr;
std::atomic<bool> running{false},paused{false},exited{true};
audio::AudioFocusLease lease;
lunaros::ManagedTaskHandle task=nullptr;
void delay() { vTaskDelay(pdMS_TO_TICKS(10)>0?pdMS_TO_TICKS(10):1); }
void report(const std::string &e) {
  std::lock_guard<std::mutex> lock(mutex);std::snprintf(state.error.data(),state.error.size(),"%s",e.c_str());
}
struct Playback {
  audio::AudioFilePlayback stream;
  unsigned native_rate=0;
  uint64_t frames=0;
  std::string error;
  bool wait() {
    if(paused.load()) {
      // Backend control stays on the focus-owning worker; an input/EOF race
      // must never pause a resident voice output after Music releases focus.
      (void)audio::local_pcm_audio_bridge_pause_output(true);
      while(running.load()&&paused.load())delay();
      if(running.load())(void)audio::local_pcm_audio_bridge_pause_output(false);
    }
    return running.load();
  }
  static bool output(void *context,const int16_t *samples,size_t frames,unsigned rate,unsigned channels) {
    auto &p=*static_cast<Playback *>(context);
    if(!p.wait()||rate!=p.native_rate||channels!=1)return false;
    if(!audio::local_pcm_audio_bridge_write_output(samples,frames,250)){p.error="music_audio_write_failed";return false;}
    p.frames+=frames;
    {std::lock_guard lock(mutex);state.position_seconds=uint32_t(p.frames/p.native_rate);}
    return true;
  }
  static bool input(const uint8_t *data,size_t size,void *context) {
    auto &p=*static_cast<Playback *>(context);
    if(!p.wait())return false;
    // Decoder errors must not overwrite the more useful output failure.
    std::string error;
    if(!p.stream.push(data,size,error)) {if(p.error.empty())p.error=error;return false;}
    return true;
  }
};
void worker(void *) {
  std::string error;
  while(running.load()) {
    Track track;
    {std::lock_guard<std::mutex> lock(mutex);track=(*state.library)[state.selected];state.position_seconds=0;}
    auto playback=std::unique_ptr<Playback>(new(std::nothrow) Playback);
    if(!playback){error="music_decoder_memory_low";break;}
    playback->native_rate=unsigned(audio::local_pcm_audio_bridge_status().output_sample_rate);
    if(playback->native_rate<8000||playback->native_rate>48000){error="music_audio_rate_invalid";break;}
    audio::PcmOutputPorts output{playback.get(),playback->native_rate,1,nullptr,Playback::output};
    if(!playback->stream.open(track.mp3?audio::AudioFileFormat::Mp3:audio::AudioFileFormat::Wave,output,error))break;
    const bool streamed=lunaros::storage_stream_file(track.path.c_str(),Playback::input,playback.get(),&error);
    const bool completed=running.load()&&streamed&&playback->stream.finish(error);
    while(!playback->stream.close())delay();
    if(!running.load()){error.clear();break;}
    if(!streamed){if(!playback->error.empty())error=playback->error;break;}
    if(!completed){if(!playback->error.empty())error=playback->error;break;}
    std::lock_guard<std::mutex> lock(mutex);
    if(state.selected+1>=state.library->size())break;
    ++state.selected;
  }
  const bool cancelled=!running.load();
  running=false;paused=false;
  // The worker still owns focus here. Stop discards queued PCM; normal EOF
  // drains it. Clear a last-moment pause before either path retires.
  if(cancelled)(void)audio::local_pcm_audio_bridge_enable_output(false);
  else (void)audio::local_pcm_audio_bridge_pause_output(false);
  // Finish submitted PCM before restoring the resident voice owner. Never
  // release focus while this worker can still write the shared backend.
  unsigned retirement_waits=0;
  for(;retirement_waits<200&&audio::local_pcm_audio_bridge_status().pending_output_samples;++retirement_waits)delay();
  if(audio::local_pcm_audio_bridge_status().pending_output_samples) {
    (void)audio::local_pcm_audio_bridge_enable_output(false);
    if(error.empty())error="music_audio_drain_failed";
  }
  {std::lock_guard<std::mutex> lock(mutex);state.playing=false;state.paused=false;state.stopping=true;}
  // Failed focus recovery retains the lease; the input owner retries stop.
  std::string release_error;
  bool released=audio::local_audio_focus().release(lease,release_error);
  for(;!released && release_error=="audio_foreground_stop_pending" && retirement_waits<200;++retirement_waits) {
    delay(); released=audio::local_audio_focus().release(lease,release_error);
  }
  if(!released)error=release_error;
  report(error);exited=true;
}
bool start(std::string &error) {
  if(!stop_local(error))return false;
  if(speaker_snapshot().receiving){error="music_stop_bluetooth_first";return false;}
  {std::lock_guard<std::mutex> lock(mutex);
    if(!state.library||state.library->empty()){error="music_library_empty";return false;}
  }
  if(!runtime||!audio::local_pcm_audio_bridge_ready()){error="music_audio_unavailable";return false;}
  if(!audio::local_audio_focus().acquire("music.local",false,true,lease,error))return false;
  if(!audio::local_pcm_audio_bridge_enable_output(true)) {
    std::string release;(void)audio::local_audio_focus().release(lease,release);error="music_audio_unavailable";return false;
  }
  running=true;paused=false;exited=false;
  {std::lock_guard<std::mutex> lock(mutex);state.playing=true;state.paused=false;state.stopping=false;state.error={};}
  if(!lunaros::create_managed_task(*runtime,{"music.local","music","playback",8192,5,lunaros::kManagedTaskIoCore,
      lunaros::ManagedTaskLifetime::Feature,
#if CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
      lunaros::ManagedTaskMemory::External,
#else
      lunaros::ManagedTaskMemory::Internal,
#endif
      false,lunaros::ManagedTaskOwnerKind::Application},worker,nullptr,&task,error)) {
    running=false;exited=true;std::string release;(void)audio::local_audio_focus().release(lease,release);
    std::lock_guard<std::mutex> lock(mutex);state.playing=false;return false;
  }
  error.clear();return true;
}
}
void configure_player(lunaros::KernelRuntime &r) {runtime=&r;}
PlaybackActivity music_activity(){
  std::lock_guard<std::mutex> lock(mutex);
  return {audio::local_pcm_audio_bridge_ready(),state.playing,state.paused,state.stopping};
}
MusicSnapshot music_snapshot() {
  MusicSnapshot out;
  {std::lock_guard<std::mutex> lock(mutex);out=state;}
  out.storage_ready=lunaros::storage_ready();out.audio_ready=audio::local_pcm_audio_bridge_ready();
  out.volume=audio::volume_percent();out.speaker=speaker_snapshot();return out;
}
bool leave_local(std::string &error){return stop_local(error);}
bool stop_local(std::string &error) {
  running=false;paused=false;
  for(unsigned i=0;!exited.load()&&i<250;++i)delay();
  if(!exited.load()){error="music_stop_pending";return false;}
  // The managed wrapper retires after the worker returns. Its handle is never
  // deleted or reused by the App before the registry acknowledges retirement.
  for(unsigned i=0;task&&runtime&&runtime->task_active("music.local")&&i<100;++i)delay();
  if(task&&runtime&&runtime->task_active("music.local")){error="music_retirement_pending";return false;}
  if(lease.generation&&!audio::local_audio_focus().release(lease,error))return false;
  task=nullptr;
  {std::lock_guard<std::mutex> lock(mutex);state.playing=false;state.paused=false;state.stopping=false;state.error={};}
  error.clear();return true;
}
bool refresh_library(std::string &error) {
  if(running.load()||!exited.load()){error="music_stop_before_refresh";return false;}
  if(!lunaros::storage_ready()){error="music_storage_unavailable";return false;}
  if(!lunaros::storage_ensure_directory(kDirectory)){error="music_directory_unavailable";return false;}
  auto library=std::make_shared<Library>();
  audio::AudioLibraryScan scan({kMaximumTracks,
      audio::AudioFileDecoder::supported(audio::AudioFileFormat::Wave),
      audio::AudioFileDecoder::supported(audio::AudioFileFormat::Mp3)},
    [](audio::AudioLibraryEntry &&entry,void *context){
      auto &tracks=*static_cast<Library *>(context);
      tracks.push_back({std::move(entry.path),std::move(entry.name),entry.format==audio::AudioFileFormat::Mp3});return true;
    },library.get());
  const bool listed=lunaros::storage_list_entries_recursive(kDirectory,4,[](const lunaros::StorageListEntry &entry,void *ctx) {
    auto &scan=*static_cast<audio::AudioLibraryScan *>(ctx);
    if(entry.is_directory||!lunaros::storage_user_file_path_allowed(entry.path.c_str()))return true;
    return scan.offer(entry.path.c_str(),entry.size_bytes);
  },&scan);
  if(!listed&&!scan.full()){error="music_library_read_failed";return false;}
  std::sort(library->begin(),library->end(),[](const Track &a,const Track &b){return a.path<b.path;});
  {std::lock_guard<std::mutex> lock(mutex);state.library=std::move(library);state.selected=0;state.position_seconds=0;state.error={};}
  error.clear();return true;
}
bool select_track(size_t index,std::string &error) {
  if(!stop_local(error))return false;
  {std::lock_guard<std::mutex> lock(mutex);
    if(!state.library||index>=state.library->size()){error="music_track_invalid";return false;}
    state.selected=index;
  }
  return start(error);
}
bool select_track_snapshot(const MusicSnapshot &snapshot,size_t index,std::string &error){
  if(snapshot.library!=music_snapshot().library){error="music_library_changed";return false;}
  return select_track(index,error);
}
bool local_action(uint32_t id,std::string &error) {
  if(id==226)return refresh_library(error);
  if(id==227)return stop_local(error);
  if(id==220||id==224) {
    const int value=std::clamp(int(audio::volume_percent())+(id==220?5:-5),0,100);
    if(!audio::set_volume_percent(uint8_t(value))){error="music_volume_failed";return false;}
    error.clear();return true;
  }
  if(id==218) {
    if(running.load()) {
      const bool pause=!paused.load();paused=pause;
      std::lock_guard<std::mutex> lock(mutex);state.paused=pause;error.clear();return true;
    }
    return start(error);
  }
  if(id==219||id==223) {
    size_t index=0;
    {std::lock_guard<std::mutex> lock(mutex);
      if(!state.library||state.library->empty()){error="music_library_empty";return false;}
      index=(state.selected+state.library->size()+(id==219?1:-1))%state.library->size();
    }
    return select_track(index,error);
  }
  error="music_action_unknown";return false;
}
}
