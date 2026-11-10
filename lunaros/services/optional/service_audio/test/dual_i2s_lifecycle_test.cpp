#include "dual_i2s_internal.h"
#include "kernel/managed_task.h"
#include "kernel/runtime.h"
#include <cassert>
#include <cstring>
#include <set>
#include <thread>
#include <mutex>
#include <iostream>
namespace lunaredu::audio {

bool stop_dual_i2s_audio(lunaredu::lunaros::KernelRuntime &);
}
using namespace lunaredu::audio;
using namespace lunaredu::audio::audio_internal;
static DualI2sConfig config{{0,1,2,3},{1,4,5,6},30,"spkr_dsp","speaker_i2s","speaker.audio"};
static int allocation=0, fail_allocation=0, pipeline_fail=0;
static bool task_fail=false, task_stuck=false, release_fail=false, registered=false,render_close_fail=false;
static bool wrapper_stuck=false;
static unsigned snapshot_reads=0,presence_reads=0;
static uint32_t ticks=0;
static size_t queue_creations=0,wake_sends=0;
static bool send_failure=false;
static std::set<void *> live;
static std::mutex live_mutex;
static bool allocated(void *p){std::lock_guard<std::mutex> lock(live_mutex);return live.count(p)!=0;}
static void *allocate(){std::lock_guard<std::mutex> lock(live_mutex);if(++allocation==fail_allocation)return nullptr;auto *p=new int(0);live.insert(p);return p;}
static void release(void*p){std::lock_guard<std::mutex> lock(live_mutex);assert(live.erase(p)==1);delete static_cast<int *>(p);}
QueueHandle_t xQueueCreate(size_t capacity,size_t){assert(capacity==(++queue_creations%2? kBlockCount:kBlockCount+1));return allocate();}
int xQueueSend(QueueHandle_t q,const void *value,TickType_t){assert(q&&allocated(q));if(*static_cast<const uint8_t *>(value)==kControlWake)++wake_sends;return send_failure?0:pdTRUE;}
void vQueueDelete(QueueHandle_t q){assert(!registered);release(q);}
SemaphoreHandle_t xSemaphoreCreateBinary(){return allocate();}
void vSemaphoreDelete(SemaphoreHandle_t s){release(s);}
esp_err_t i2s_channel_disable(i2s_chan_handle_t h){assert(allocated(h));return ESP_OK;}
esp_err_t i2s_del_channel(i2s_chan_handle_t h){if(release_fail)return ESP_FAIL;release(h);return ESP_OK;}
void vTaskDelay(TickType_t t){ticks+=t;if(!task_stuck){g_output_exited.store(true);if(!wrapper_stuck)registered=false;}}
namespace lunaredu::audio { bool valid_dual_i2s_config(const DualI2sConfig &){return true;} }
namespace lunaredu::audio::audio_internal {
DualI2sConfig g_config;
uint32_t now_ms(){return ticks;}
void output_task(void*){}
bool prepare_audio_pipeline(){g_left_tx=allocate();if(pipeline_fail==1)return false;g_right_tx=allocate();return pipeline_fail!=2;}
void reset_audio_control(){}
bool close_audio_pipeline(){return !render_close_fail;}
}
namespace lunaredu::lunaros {
KernelRuntime::KernelRuntime() = default;
KernelRuntime::~KernelRuntime() = default;
bool create_managed_task(KernelRuntime &,const ManagedTaskDefinition &d,ManagedTaskFunction,void*,ManagedTaskHandle*h,std::string&) {
 assert(std::strcmp(d.owner_service_id,"speaker.audio")==0);
 if(task_fail)return false;
 registered=true;*h=reinterpret_cast<ManagedTaskHandle>(uintptr_t(1));return true;
}
ManagedTaskRegistrySnapshot KernelRuntime::task_snapshot() const {++snapshot_reads;ManagedTaskRegistrySnapshot s;s.complete=false;s.active_tasks=registered?1:0;return s;}
bool KernelRuntime::task_active(const char *id,ManagedTaskOwnerKind kind,const char *owner) const {
 ++presence_reads;assert(std::strcmp(id,"speaker_i2s")==0);
 assert(kind==ManagedTaskOwnerKind::Service&&std::strcmp(owner,"speaker.audio")==0);return registered;
}
}
int main(){
 lunaredu::lunaros::KernelRuntime runtime;
 for(int failure=1;failure<=4;++failure){allocation=0;fail_allocation=failure;assert(!setup_dual_i2s_audio(runtime,config));assert(live.empty());}
 fail_allocation=0;
 for(int failure=1;failure<=2;++failure){pipeline_fail=failure;assert(!setup_dual_i2s_audio(runtime,config));assert(live.empty());}
 pipeline_fail=0;task_fail=true;assert(!setup_dual_i2s_audio(runtime,config));assert(live.empty());task_fail=false;
 assert(setup_dual_i2s_audio(runtime,config));const auto owned=live.size();assert(setup_dual_i2s_audio(runtime,config));assert(live.size()==owned);
 const auto wakes=wake_sends;
 for(int i=0;i<1000;++i)wake_output_control();
 assert(wake_sends==wakes+1); // Coalesce control bursts without consuming PCM slots.
 assert(!consume_output_control(0)&&consume_output_control(kControlWake));
 wake_output_control();assert(wake_sends==wakes+2);
 assert(consume_output_control(kControlWake));send_failure=true;wake_output_control();send_failure=false;
 wake_output_control();assert(wake_sends==wakes+4); // A failed enqueue permits retry.
 assert(consume_output_control(kControlWake));
 task_stuck=true;assert(!stop_dual_i2s_audio(runtime));assert(live.size()==owned&&g_output_task);
 task_stuck=false;wrapper_stuck=true;assert(!stop_dual_i2s_audio(runtime));
 assert(g_output_exited&&registered&&live.size()==owned&&g_output_task&&g_left_tx&&g_right_tx&&g_free_queue);
 wrapper_stuck=false;render_close_fail=true;
 assert(!stop_dual_i2s_audio(runtime)&&g_left_tx&&g_right_tx&&g_free_queue);
 render_close_fail=false;release_fail=true;assert(!stop_dual_i2s_audio(runtime));assert(g_left_tx&&g_right_tx&&g_free_queue);assert(!setup_dual_i2s_audio(runtime,config));
 release_fail=false;assert(stop_dual_i2s_audio(runtime));assert(live.empty());assert(stop_dual_i2s_audio(runtime));
 assert(setup_dual_i2s_audio(runtime,config));
 std::atomic<bool> done{false};
 std::thread controls([&]{while(!done.load()){wake_output_control();(void)consume_output_control(kControlWake);}});
 assert(stop_dual_i2s_audio(runtime));done=true;controls.join();assert(live.empty());
 wake_output_control(); // Safe after the queue has been released.
 assert(snapshot_reads==0&&presence_reads>0);
 std::cout<<"PASS dual I2S retirement: allocation failures, worker/wrapper gap, snapshot OOM, retained hardware, retry and control concurrency\n";

}
