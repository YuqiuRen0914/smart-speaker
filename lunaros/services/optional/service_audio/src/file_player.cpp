#include "lunaros_audio_file_player.h"
#include "lunaros_audio_focus.h"
#include "kernel/managed_task.h"
#include "kernel/runtime.h"
#include "kernel/task_signal.h"
#include "lunaros_audio_file_playback.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
namespace lunaredu::audio {
namespace {
std::mutex mutex;
FilePlayerConfig config;
lunaros::KernelRuntime *runtime=nullptr;
lunaros::ManagedTaskHandle task=nullptr;
FILE *pending=nullptr;
bool busy=false;
AudioFileFormat pending_format=AudioFileFormat::Mp3;
std::atomic<uint32_t> generation{0};
std::atomic<bool> closing{false},exited{true},paused{false};
std::atomic<FilePlayerState> state{FilePlayerState::Idle};
void event(FilePlayerEvent e){
  if(config.event){FilePlayerEventContext context{e,config.context};config.event(&context);}
}
void delay(){vTaskDelay(pdMS_TO_TICKS(5)>0?pdMS_TO_TICKS(5):1);}
struct Playback {
  AudioFilePlayback stream;
  uint32_t epoch=0;
  bool started=false,pause_observed=false;
  bool current()const{return !closing.load()&&epoch==generation.load();}
  bool wait(){
    while(current()){
      const bool pause=paused.load();
      if(pause!=pause_observed){
        pause_observed=pause;
        if(config.pause_output(pause)!=ESP_OK)return false;
        state=pause?FilePlayerState::Paused:FilePlayerState::Playing;
        event(pause?FilePlayerEvent::Paused:FilePlayerEvent::Playing);
      }
      if(!pause)return true;
      lunaros::task_signal::wait_ms(lunaros::task_signal::kWaitForever);
    }
    return false;
  }
  static bool prepare(void *context,unsigned rate,unsigned channels){
    auto &p=*static_cast<Playback *>(context);
    return p.wait()&&config.reconfigure(rate,channels)==ESP_OK&&p.current();
  }
  static bool write_pcm(void *context,const int16_t *samples,size_t frames,unsigned,unsigned channels){
    auto &p=*static_cast<Playback *>(context);
    if(!p.wait())return false;
    if(!p.started){
      if(config.mute(AudioMute::Unmute)!=ESP_OK||!p.current())return false;
      p.started=true;
    }
    size_t written=0;const size_t bytes=frames*channels*sizeof(int16_t);
    return config.write(samples,bytes,&written,250,config.context)==ESP_OK&&written==bytes;
  }
};
void worker(void *){
  while(!closing.load()){
    FILE *file=nullptr;AudioFileFormat format;uint32_t epoch;
    {
      std::lock_guard<std::mutex> lock(mutex);
      file=pending;pending=nullptr;format=pending_format;epoch=generation.load();busy=file!=nullptr;
    }
    if(!file){lunaros::task_signal::wait_ms(lunaros::task_signal::kWaitForever);continue;}
    auto playback=std::unique_ptr<Playback>(new(std::nothrow) Playback);
    std::string error;
    AudioFocusLease lease;
    const bool owns_output=config.focus->acquire(config.focus_owner,false,true,lease,error);
    bool okay=bool(playback)&&owns_output;
    if(playback&&okay){
      playback->epoch=epoch;state=FilePlayerState::Playing;event(FilePlayerEvent::Playing);
      PcmOutputPorts output{playback.get(),0,config.force_stereo?2u:0u,Playback::prepare,Playback::write_pcm,config.effects};
      okay=playback->stream.open(format,output,error);
      std::array<uint8_t,2048> input{};
      while(okay&&playback->wait()){
        const size_t count=std::fread(input.data(),1,input.size(),file);
        if(count){okay=playback->stream.push(input.data(),count,error);continue;}
        okay=!std::ferror(file)&&playback->stream.finish(error);break;
      }
    }
    std::fclose(file);
    // Do not allow a new generation to overwrite queued PCM from this file.
    // Failed retirement remains on this owner and is retried, including shutdown.
    bool retired=false;
    do {
      retired=(!playback||playback->stream.close())&&
        (!owns_output||(closing.load()||epoch!=generation.load()||!okay?config.discard(100):config.drain(100)));
      if(!retired)delay();
    }while(!retired);
    if(owns_output)(void)config.mute(AudioMute::Mute);
    while(lease.generation&&!config.focus->release(lease,error))delay();
    {
      // Event callbacks are borrowed, non-reentrant notifications. Serialize
      // publication with command admission so an old EOF cannot clear a new song.
      std::lock_guard<std::mutex> lock(mutex);
      busy=false;
      if(!pending){
        state=FilePlayerState::Idle;
        if(!closing.load()&&epoch==generation.load()&&!okay)event(FilePlayerEvent::Failed);
        event(FilePlayerEvent::Idle);
      }
    }
  }
  std::lock_guard<std::mutex> lock(mutex);
  exited.store(true,std::memory_order_release);
}
}
bool start_file_player(lunaros::KernelRuntime &owner,const FilePlayerConfig &ports,std::string &error){
  std::lock_guard<std::mutex> lock(mutex);
  if(task){error="file_player_already_bound";return false;}
  if(!valid_pcm_effects(ports.effects)||!ports.write||!ports.reconfigure||!ports.mute||!ports.pause_output||!ports.drain||!ports.discard||!ports.task_id||!ports.service_id||!ports.focus||!ports.focus_owner){error="file_player_ports_invalid";return false;}
  config=ports;runtime=&owner;closing=false;paused=false;exited=false;state=FilePlayerState::Idle;
  if(!lunaros::create_managed_task(owner,{.id=config.task_id,.owner_service_id=config.service_id,.role="file_decode",
       .stack_bytes=8192,.priority=config.priority,.core_affinity=config.core,
       .lifetime=lunaros::ManagedTaskLifetime::Resident,.memory=lunaros::ManagedTaskMemory::Internal},worker,nullptr,&task,error)){
    config={};runtime=nullptr;exited=true;return false;
  }
  error.clear();return true;
}
bool stop_file_player(std::string &error){
  std::lock_guard<std::mutex> lock(mutex);
  if(!task){error.clear();return true;}
  closing=true;++generation;paused=false;
  if(pending){std::fclose(pending);pending=nullptr;}
  if(!exited.load(std::memory_order_acquire))lunaros::task_signal::notify(task);
  if(!exited.load(std::memory_order_acquire)){error="file_player_stop_pending";return false;}
  if(runtime->task_active(config.task_id,lunaros::ManagedTaskOwnerKind::Service,config.service_id)){
    error="file_player_task_pending";return false;
  }
  task=nullptr;runtime=nullptr;config={};state=FilePlayerState::Idle;error.clear();return true;
}
esp_err_t file_player_play(FILE *file,AudioFileFormat format){
  std::lock_guard<std::mutex> lock(mutex);
  if(!file||!task||closing.load()||pending==file)return ESP_ERR_INVALID_STATE;
  if(!AudioFileDecoder::supported(format))return ESP_ERR_NOT_SUPPORTED;
  // Transport selections coalesce to the latest request. An unstarted FILE
  // belongs exclusively to this slot, so closing it cannot race a decoder.
  if(pending)std::fclose(pending);
  ++generation;pending=file;pending_format=format;paused=false;
  // Publish admission immediately; checked stop must not confuse a queued file
  // with an already retired Idle player.
  state=FilePlayerState::Playing;lunaros::task_signal::notify(task);return ESP_OK;
}
esp_err_t file_player_pause(){
  std::lock_guard<std::mutex> lock(mutex);
  if(!task||closing.load()||state.load()==FilePlayerState::Idle)return ESP_ERR_INVALID_STATE;
  paused=true;lunaros::task_signal::notify(task);return ESP_OK;
}
esp_err_t file_player_resume(){
  std::lock_guard<std::mutex> lock(mutex);
  if(!task||closing.load()||state.load()==FilePlayerState::Idle)return ESP_ERR_INVALID_STATE;
  paused=false;lunaros::task_signal::notify(task);return ESP_OK;
}
esp_err_t file_player_stop(){
  std::lock_guard<std::mutex> lock(mutex);
  if(!task||closing.load())return ESP_ERR_INVALID_STATE;
  ++generation;paused=false;
  if(pending){std::fclose(pending);pending=nullptr;}
  if(!busy){state=FilePlayerState::Idle;event(FilePlayerEvent::Idle);}
  lunaros::task_signal::notify(task);return ESP_OK;
}
FilePlayerState file_player_state(){return state.load();}
}
