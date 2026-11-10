#include "lunaros_network_service_id.h"
#include "music_output.h"
#if LUNAROS_NETWORK_BLUETOOTH_AUDIO
#include "bluetooth_internal.h"
#include "music_output.h"
#include "pcm_packet_queue.h"
#include "lunaros_audio_focus.h"
#include "kernel/managed_task.h"
#include "kernel/runtime.h"
#include "lunaros_audio_bridge.h"
#include "lunaros_audio_pcm_output.h"
#include <atomic>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <sdkconfig.h>

namespace lunaredu::lunaros::bluetooth_service_internal {
using audio::AudioFocusLease;
using audio::local_audio_focus;
namespace {
constexpr size_t kBlocks=kPcmBlocks;
using Block=PcmBlock;
Block *blocks=nullptr;
std::atomic<uint32_t> read_index{0},write_index{0};
std::atomic<bool> enabled{false},exited{true};
std::atomic<unsigned> audio_format{(44100u<<2)|2};
TaskHandle_t task=nullptr;
AudioFocusLease lease;
std::mutex pool_mutex;
int native_rate=24000;
void worker(void *) {
  audio::PcmOutputAdapter output;
  audio::PcmOutputPorts ports{nullptr,unsigned(native_rate),1,nullptr,
    [](void *,const int16_t *samples,size_t frames,unsigned,unsigned){
      return enabled.load()&&audio::local_pcm_audio_bridge_write_output(samples,frames,20);
    }};
  bool configured=false;
  while(enabled.load()) {
    const uint32_t read=read_index.load();
    if(read==write_index.load(std::memory_order_acquire)){vTaskDelay(pdMS_TO_TICKS(2));continue;}
    const auto &block=blocks[read%kBlocks];
    if(!configured)configured=output.configure(ports);
    if(!configured||!output.write_bytes(block.data,block.size,block.sample_rate,block.channels)) {
      std::lock_guard<std::mutex> lock(mutex);++state.audio_drops;++state.revision;
    }
    read_index.store(read+1,std::memory_order_release);
  }
  while(!output.close())vTaskDelay(1);
  exited=true;
}
}
void music_audio_format(unsigned rate,unsigned channels){
  if((rate==16000||rate==32000||rate==44100||rate==48000)&&(channels==1||channels==2))
    audio_format=(rate<<2)|channels;
}
void music_receive_pcm(const uint8_t *data,uint32_t size) {
  std::unique_lock<std::mutex> pool_lock(pool_mutex,std::try_to_lock);
  if(!pool_lock.owns_lock()||!enabled.load()||!data||!size)return;
  auto write=write_index.load();
  const unsigned format=audio_format.load();
  if(!queue_pcm_packet(blocks,data,size,read_index.load(std::memory_order_acquire),write,format>>2,format&3)) {
    std::lock_guard<std::mutex> lock(mutex);++state.audio_drops;return;
  }
  write_index.store(write,std::memory_order_release);
  std::lock_guard<std::mutex> lock(mutex);++state.audio_packets;
}
bool enable_music_output(bool value,std::string &error) {
  if(value&&enabled.load()){error.clear();return true;}
  if(!value) {
    enabled=false;
    for(unsigned i=0;!exited.load()&&i<100;++i)vTaskDelay(pdMS_TO_TICKS(5));
    if(!exited.load()){error="bluetooth_music_stop_pending";return false;}
    if(task&&runtime) {
      bool retiring=true;
      for(unsigned attempt=0;retiring&&attempt<100;++attempt) {
        retiring=false;
        retiring=runtime->task_active("bluetooth.music");
        if(retiring)vTaskDelay(pdMS_TO_TICKS(5));
      }
      if(retiring){error="bluetooth_music_retirement_pending";return false;}
    }
    if(lease.generation&&!local_audio_focus().release(lease,error))return false;
    {std::lock_guard<std::mutex> pool_lock(pool_mutex);
      if(blocks){heap_caps_free(blocks);blocks=nullptr;}
    }
    task=nullptr;
    error.clear();return true;
  }
  if(!runtime||!audio::local_pcm_audio_bridge_ready()){error="bluetooth_audio_unavailable";return false;}
  if(task||lease.generation){if(!enable_music_output(false,error))return false;}
  if(!local_audio_focus().acquire("bluetooth.music",false,true,lease,error))return false;
  native_rate=audio::local_pcm_audio_bridge_status().output_sample_rate;
  if(native_rate<8000||native_rate>48000){(void)local_audio_focus().release(lease,error);error="bluetooth_audio_rate_invalid";return false;}
  if(!blocks)blocks=static_cast<Block *>(heap_caps_malloc(sizeof(Block)*kBlocks,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!blocks){(void)local_audio_focus().release(lease,error);error="bluetooth_audio_pool_failed";return false;}
  read_index=write_index=0;enabled=true;exited=false;
  if(!create_managed_task(*runtime,{"bluetooth.music",kNetworkRuntimeServiceId,"audio",8192,5,kManagedTaskIoCore,ManagedTaskLifetime::Feature,
#if CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
      ManagedTaskMemory::External,
#else
      ManagedTaskMemory::Internal,
#endif
      false},worker,nullptr,&task,error)) {
    enabled=false;exited=true;std::string release;(void)local_audio_focus().release(lease,release);return false;
  }
  (void)audio::local_pcm_audio_bridge_enable_output(true);error.clear();return true;
}
} // namespace lunaredu::lunaros::bluetooth_service_internal

#else
namespace lunaredu::lunaros::bluetooth_service_internal {
bool enable_music_output(bool enabled,std::string &error) {
  if(enabled){error="bluetooth_music_unsupported";return false;}
  error.clear();return true;
}
void music_audio_format(unsigned,unsigned){}
void music_receive_pcm(const uint8_t *,uint32_t){}
}
#endif
