#include "lunaros_audio_file_player.h"
#include "lunaros_audio_focus.h"
#include "kernel/managed_task.h"
#include "kernel/runtime.h"
#include "kernel/task_signal.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
using namespace lunaredu;
using namespace lunaredu::audio;
namespace {
std::thread worker;
std::mutex wake_mutex;
std::condition_variable wake;
bool notified=false;
std::atomic<unsigned> periodic_waits{0};
std::atomic<bool> registered{false},retirement_allowed{true},writer_allowed{true};
std::atomic<bool> wrapper_retirement_allowed{true},function_exited{false};
std::atomic<unsigned> snapshot_reads{0},presence_reads{0};
std::atomic<unsigned> writes{0},pauses{0},resumes{0},unmutes{0},idles{0},failures{0},discards{0},drains{0};
std::atomic<bool> validate_stereo{true};
AudioFocusBroker broker;
std::atomic<bool> focus_allowed{true};
std::atomic<unsigned> focus_acquires{0},focus_releases{0};
unsigned configured_channels=0;
void delay(){std::this_thread::sleep_for(std::chrono::milliseconds(1));}
template<class F>void until(F predicate){for(int i=0;i<5000&&!predicate();++i)delay();assert(predicate());}
FILE *wave(bool mono=false){
  FILE *f=std::tmpfile();assert(f);
  const unsigned channels=mono?1:2,frames=24000,bytes=frames*channels*2;
  const auto text=[&](const char *s){std::fwrite(s,1,4,f);};
  const auto number=[&](unsigned n,unsigned size){for(unsigned i=0;i<size;++i)std::fputc(int((n>>(8*i))&255),f);};
  text("RIFF");number(bytes+36,4);text("WAVE");text("fmt ");number(16,4);number(1,2);number(channels,2);
  number(16000,4);number(16000*channels*2,4);number(channels*2,2);number(16,2);text("data");number(bytes,4);
  for(unsigned i=0;i<frames;++i){number(1234,2);if(!mono)number(unsigned(uint16_t(-2345)),2);}
  std::rewind(f);return f;
}
FilePlayerConfig ports(){
  FilePlayerConfig p;
  p.write=[](const void *data,size_t size,size_t *written,uint32_t,void *){
    until([]{return writer_allowed.load();});
    const auto *pcm=static_cast<const int16_t *>(data);
    assert(size%4==0&&configured_channels==2);
    for(size_t i=0;i<size/2;i+=2){assert(pcm[i]==1234);assert(pcm[i+1]==(validate_stereo.load()?-2345:1234));}
    ++writes;*written=size;std::this_thread::sleep_for(std::chrono::milliseconds(2));return ESP_OK;
  };
  p.reconfigure=[](uint32_t rate,unsigned channels){assert(rate==16000);configured_channels=channels;return ESP_OK;};
  p.mute=[](AudioMute m){if(m==AudioMute::Unmute)++unmutes;return ESP_OK;};
  p.pause_output=[](bool paused){if(paused)++pauses;else ++resumes;return ESP_OK;};
  p.discard=[](uint32_t){++discards;return retirement_allowed.load();};
  p.drain=[](uint32_t){++drains;return retirement_allowed.load();};
  p.event=[](FilePlayerEventContext *e){if(e->event==FilePlayerEvent::Idle)++idles;if(e->event==FilePlayerEvent::Failed)++failures;};
  p.force_stereo=true;p.focus=&broker;return p;
}
void stop_service(){std::string error;until([&]{return stop_file_player(error);});worker.join();}
}
void vTaskDelay(uint32_t ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
namespace lunaredu::lunaros::task_signal {
void notify(void *){std::lock_guard<std::mutex> lock(wake_mutex);notified=true;wake.notify_all();}
uint32_t wait_ms(uint32_t ms){if(ms!=kWaitForever)++periodic_waits;std::unique_lock<std::mutex> lock(wake_mutex);wake.wait_for(lock,std::chrono::milliseconds(ms),[]{return notified;});const bool result=notified;notified=false;return result;}
}
namespace lunaredu::lunaros {
KernelRuntime::KernelRuntime()=default;
KernelRuntime::~KernelRuntime()=default;
bool create_managed_task(KernelRuntime &,const ManagedTaskDefinition &d,ManagedTaskFunction f,void *ctx,ManagedTaskHandle *handle,std::string &error){
  assert(d.stack_bytes==8192&&d.core_affinity==1&&d.priority==8);
  registered=true;function_exited=false;*handle=reinterpret_cast<ManagedTaskHandle>(uintptr_t(1));
  worker=std::thread([=]{f(ctx);function_exited=true;until([]{return wrapper_retirement_allowed.load();});registered=false;});error.clear();return true;
}
ManagedTaskRegistrySnapshot KernelRuntime::task_snapshot()const{
  ++snapshot_reads;ManagedTaskRegistrySnapshot out;out.complete=false;out.active_tasks=registered?1:0;return out;
}
bool KernelRuntime::task_active(const char *id,ManagedTaskOwnerKind kind,const char *owner)const{
  ++presence_reads;assert(std::strcmp(id,"audio.file_player")==0);
  assert(kind==ManagedTaskOwnerKind::Service&&std::strcmp(owner,"audio")==0);return registered.load();
}
}
// Broker behavior has its own real Core tests. This test drives actual file
// shared playback ownership and worker cancellation/retirement. The decoder
// and GMF are explicit owner seams; actual parsing runs in the IDF fixture.
namespace lunaredu::audio {
bool AudioFocusBroker::acquire(const char *,bool microphone,bool speaker,AudioFocusLease &lease,std::string &){
  assert(!microphone&&speaker);if(!focus_allowed)return false;++focus_acquires;lease.generation=1;return true;
}
bool AudioFocusBroker::release(AudioFocusLease &lease,std::string &){assert(lease.generation==1);++focus_releases;lease={};return true;}
}
int main(){
  lunaros::KernelRuntime runtime;std::string error;
  assert(!start_file_player(runtime,{},error));
  assert(start_file_player(runtime,ports(),error));assert(!start_file_player(runtime,ports(),error));
  writer_allowed=false;assert(file_player_play(wave(),AudioFileFormat::Wave)==ESP_OK);
  until([]{return file_player_state()==FilePlayerState::Playing;});writer_allowed=true;
  until([]{return writes.load()>2;});
  assert(file_player_pause()==ESP_OK);until([]{return file_player_state()==FilePlayerState::Paused;});
  const auto count=writes.load(),clock_resets=unmutes.load();std::this_thread::sleep_for(std::chrono::milliseconds(20));assert(writes==count);
  assert(file_player_resume()==ESP_OK);until([]{return resumes.load()==1;});assert(unmutes==clock_resets);
  // Stop a genuinely paused decoder without resuming; failed physical drain
  // retains Playing/Paused until it is safe to acknowledge Idle.
  assert(file_player_pause()==ESP_OK);until([]{return pauses.load()==2;});retirement_allowed=false;
  assert(file_player_stop()==ESP_OK);until([]{return discards.load()>0;});assert(file_player_state()!=FilePlayerState::Idle);
  assert(!stop_file_player(error)&&error=="file_player_stop_pending");
  retirement_allowed=true;stop_service();assert(file_player_state()==FilePlayerState::Idle);
  // Actual mono WAV is expanded once into stereo; natural EOF drains output.
  validate_stereo=false;assert(start_file_player(runtime,ports(),error));
  const auto before=idles.load();assert(file_player_play(wave(true),AudioFileFormat::Wave)==ESP_OK);
  until([&]{return idles.load()>before;});assert(drains>0&&failures==0);stop_service();
  // Replacing an active file must discard its tail before admitting PCM from
  // the next file; no old Idle event is published for that replacement.
  validate_stereo=true;assert(start_file_player(runtime,ports(),error));
  const auto starting=writes.load();assert(file_player_play(wave(),AudioFileFormat::Wave)==ESP_OK);
  until([&]{return writes.load()>starting;});retirement_allowed=false;
  const auto idle_before=idles.load(),discard_before=discards.load();
  assert(file_player_play(wave(),AudioFileFormat::Wave)==ESP_OK);
  until([&]{return discards.load()>discard_before;});assert(idles==idle_before);
  retirement_allowed=true;until([&]{return idles.load()>idle_before;});stop_service();
  // Denied focus cannot mute, drain or write another output owner's device.
  focus_allowed=false;const auto old_writes=writes.load(),old_discards=discards.load(),old_unmutes=unmutes.load();
  assert(start_file_player(runtime,ports(),error));const auto old_failures=failures.load();
  assert(file_player_play(wave(),AudioFileFormat::Wave)==ESP_OK);
  until([&]{return failures.load()>old_failures;});stop_service();
  assert(writes==old_writes&&discards==old_discards&&unmutes==old_unmutes);
  assert(focus_acquires==focus_releases);
  assert(file_player_stop()==ESP_ERR_INVALID_STATE);
  wrapper_retirement_allowed=false;
  assert(start_file_player(runtime,ports(),error));
  assert(!stop_file_player(error));
  until([]{return function_exited.load();});
  for(unsigned retry=0;retry<10;++retry)
    assert(!stop_file_player(error)&&error=="file_player_task_pending");
  assert(registered&&!start_file_player(runtime,ports(),error));
  wrapper_retirement_allowed=true;stop_service();
  assert(snapshot_reads==0&&presence_reads>0);
  std::cout<<"PASS shared file player: native stereo, mono expansion, pause clock, paused stop, replacement, EOF drain, retained shutdown and restart\n";
  std::cout<<"PASS file-player retirement: diagnostic snapshot OOM cannot acknowledge a live owner wrapper\n";
  assert(periodic_waits==0); // Idle and paused owners wake only for commands.
}
