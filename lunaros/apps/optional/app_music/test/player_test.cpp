#include "lunaredu_music_player.h"
#include "lunaros_audio_bridge.h"
#include "lunaros_audio_volume.h"
#include "lunaros_storage.h"
#include "kernel/runtime.h"
#include "lunaros_audio_focus.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <functional>
#include <freertos/task.h>
#include "rate_cvt_test_control.h"
namespace {
#if defined(_WIN32) && !defined(_GLIBCXX_HAS_GTHREADS)
HANDLE task=nullptr;
lunaredu::lunaros::ManagedTaskFunction task_function=nullptr;
void *task_context=nullptr;
#else
std::thread task;
#endif
std::atomic<bool> active{false},focus{false},release_failure{false},write_failure{false};
std::atomic<unsigned> writes{0},opens{0};
std::atomic<bool> output_paused{false};
bool ready=true,many=false;
uint8_t volume=35;
void wait(const std::function<bool()> &condition) {
  for(unsigned i=0;i<4000&&!condition();++i)vTaskDelay(1);
  assert(condition());
}
}
void vTaskDelay(TickType_t) {
#if defined(_WIN32) && !defined(_GLIBCXX_HAS_GTHREADS)
  Sleep(1);
#else
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
}
void join_task() {
#if defined(_WIN32) && !defined(_GLIBCXX_HAS_GTHREADS)
  if(task){WaitForSingleObject(task,INFINITE);CloseHandle(task);task=nullptr;}
#else
  if(task.joinable())task.join();
#endif
}
namespace lunaredu::lunaros {
KernelRuntime::KernelRuntime()=default;
KernelRuntime::~KernelRuntime() {join_task();}
bool KernelRuntime::task_active(const char *)const{return active;}
bool create_managed_task(KernelRuntime &,const ManagedTaskDefinition &d,ManagedTaskFunction f,void *c,ManagedTaskHandle *h,std::string &e) {
  assert(d.owner_kind==ManagedTaskOwnerKind::Application&&!std::strcmp(d.owner_service_id,"music"));
  assert(d.lifetime==ManagedTaskLifetime::Feature&&focus);
  join_task();
  active=true;*h=reinterpret_cast<ManagedTaskHandle>(1);
#if defined(_WIN32) && !defined(_GLIBCXX_HAS_GTHREADS)
  task_function=f;task_context=c;
  task=CreateThread(nullptr,0,[](LPVOID)->DWORD {task_function(task_context);active=false;return 0;},nullptr,0,nullptr);assert(task);
#else
  task=std::thread([=]{f(c);active=false;});
#endif
  e.clear();return true;
}
} // namespace lunaredu::lunaros
namespace lunaredu::audio {
AudioFocusBroker &local_audio_focus(){static AudioFocusBroker b;return b;}
bool AudioFocusBroker::acquire(const char *,bool mic,bool speaker,AudioFocusLease &l,std::string &e) {
  assert(!mic&&speaker&&!focus);focus=true;l.generation=1;e.clear();return true;
}
bool AudioFocusBroker::release(AudioFocusLease &l,std::string &e) {
  if(release_failure.exchange(false)){e="resident_resume_failed";return false;}
  l.generation=0;focus=false;e.clear();return true;
}
} // namespace lunaredu::audio
namespace lunaredu::lunaros {
bool storage_ready(){return ready;}
bool storage_ensure_directory(const char *p){assert(!std::strcmp(p,"/performances/music"));return ready;}
bool storage_user_file_path_allowed(const char *p){return std::strncmp(p,"/performances/",14)==0;}
bool storage_list_entries_recursive(const char *,uint8_t depth,StorageEntryVisitor v,void *c) {
  assert(depth==4);
  if(many){for(unsigned i=0;i<100;++i)if(!v({"/performances/music/"+std::to_string(i)+".wav",256,false,0},c))return false;return true;}
  for(const auto &p:{"/performances/music/b.WAV","/performances/music/a.wav","/performances/music/skip.txt","/performances/music/skip.mp3","/private/leak.wav"})
    if(!v({p,256,false,0},c))return false;
  return true;
}
bool storage_stream_file(const char *,StorageFileChunkWriter writer,void *c,std::string *e) {
  ++opens;
  std::vector<uint8_t> file(44+16000*2,0);
  std::memcpy(file.data(),"RIFF",4);std::memcpy(file.data()+8,"WAVEfmt ",8);file[16]=16;file[20]=1;file[22]=1;
  const uint32_t rate=16000,size=32000,riff=uint32_t(file.size()-8),bytes=32000;
  std::memcpy(file.data()+4,&riff,4);std::memcpy(file.data()+24,&rate,4);std::memcpy(file.data()+28,&bytes,4);
  file[32]=2;file[34]=16;std::memcpy(file.data()+36,"data",4);std::memcpy(file.data()+40,&size,4);
  for(size_t i=0;i<file.size();i+=137)if(!writer(file.data()+i,std::min(size_t(137),file.size()-i),c)){*e="file_stream_cancelled";return false;}
  e->clear();return true;
}
}
namespace lunaredu::audio {
bool local_pcm_audio_bridge_ready(){return true;}
LocalPcmAudioBridgeStatus local_pcm_audio_bridge_status(){LocalPcmAudioBridgeStatus s;s.output_sample_rate=24000;return s;}
bool local_pcm_audio_bridge_enable_output(bool enable){if(!enable)output_paused=false;return true;}
bool local_pcm_audio_bridge_pause_output(bool pause){output_paused=pause;return true;}
bool local_pcm_audio_bridge_write_output(const int16_t *,size_t size,uint32_t) {
  assert(size<=256&&focus);++writes;vTaskDelay(1);return !write_failure;
}
uint8_t volume_percent(){return volume;}
bool set_volume_percent(uint8_t v){volume=v;return true;}
}
int main() {
  using namespace lunaredu;std::string e;lunaros::KernelRuntime r;music::configure_player(r);
  ready=false;assert(!music::refresh_library(e)&&e=="music_storage_unavailable");ready=true;
  assert(music::refresh_library(e));auto s=music::music_snapshot();
  assert(s.library->size()==2&&s.library->front().title=="a.wav"&&!s.speaker.supported);
  const auto saved=s.library;
  assert(music::local_action(218,e));wait([]{return writes>3;});
  assert(music::local_action(218,e)&&music::music_snapshot().paused);wait([]{return output_paused.load();});
  vTaskDelay(1);const auto before=writes.load();vTaskDelay(5);assert(writes<=before+1);
  assert(!music::refresh_library(e)&&e=="music_stop_before_refresh");
  assert(music::local_action(220,e)&&volume==40);
  assert(music::local_action(218,e));wait([]{return !output_paused.load();});
  wait([]{return !active.load();});assert(opens==2&&!focus); // Automatic next then end.
  assert(music::stop_local(e));
  const auto selected_before=writes.load();
  assert(music::select_track(1,e));wait([&]{return writes>selected_before+3;});
  assert(music::local_action(218,e));wait([]{return output_paused.load();});
  assert(music::stop_local(e)&&!active&&!focus&&!output_paused);
  write_failure=true;assert(music::select_track(0,e));wait([]{return !active.load();});
  assert(std::string(music::music_snapshot().error.data())=="music_audio_write_failed");write_failure=false;
  assert(music::stop_local(e));
  assert(music::select_track(0,e));wait([&]{return writes>before+2;});release_failure=true;
  assert(music::stop_local(e)); // Worker retains failure, owner retries and recovers.
  assert(!focus&&!active);
  rate_cvt_test::fail_process=true;
  assert(music::select_track(0,e));wait([]{return !active.load();});
  assert(std::string(music::music_snapshot().error.data())=="audio_rate_converter_process_failed"&&!focus);
  rate_cvt_test::fail_process=false;assert(music::stop_local(e));
  rate_cvt_test::fail_open=true;
  assert(music::select_track(0,e));wait([]{return !active.load();});
  assert(std::string(music::music_snapshot().error.data())=="audio_rate_converter_open_failed"&&!focus);
  rate_cvt_test::fail_open=false;assert(music::stop_local(e));
  many=true;assert(music::refresh_library(e)&&music::music_snapshot().library->size()==64&&saved->size()==2);
  assert(!music::select_track(99,e)&&e=="music_track_invalid");
  assert(!music::speaker_action(222,{},e)&&e=="bluetooth_music_unsupported");
  std::cout<<"PASS local playback without Bluetooth, bounded library, pause/resume, automatic next, volume, cancellation and focus recovery\n";
}
