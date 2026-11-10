#include "audio_output_internal.h"
#include "lunaredu_lunaros/kernel/managed_task.h"
#include <cassert>
#include <cstring>
#include <set>
namespace smart_speaker { bool setup_dual_i2s_audio();bool stop_dual_i2s_audio(); }
using namespace smart_speaker;
using namespace smart_speaker::audio_internal;
static int allocation=0, fail_allocation=0, pipeline_fail=0;
static bool task_fail=false, task_stuck=false, release_fail=false, registered=false;
static uint32_t ticks=0;
static std::set<void *> live;
static void *allocate(){if(++allocation==fail_allocation)return nullptr;auto *p=new int(0);live.insert(p);return p;}
static void release(void*p){assert(live.erase(p)==1);delete static_cast<int *>(p);}
QueueHandle_t xQueueCreate(size_t,size_t){return allocate();}
int xQueueSend(QueueHandle_t q,const void *,TickType_t){assert(q);return pdTRUE;}
void vQueueDelete(QueueHandle_t q){assert(!registered);release(q);}
SemaphoreHandle_t xSemaphoreCreateBinary(){return allocate();}
void vSemaphoreDelete(SemaphoreHandle_t s){release(s);}
esp_err_t i2s_channel_disable(i2s_chan_handle_t h){assert(live.count(h));return ESP_OK;}
esp_err_t i2s_del_channel(i2s_chan_handle_t h){if(release_fail)return ESP_FAIL;release(h);return ESP_OK;}
void vTaskDelay(TickType_t t){ticks+=t;if(!task_stuck){g_output_exited.store(true);registered=false;}}
namespace smart_speaker::audio_internal {
uint32_t now_ms(){return ticks;}
void output_task(void*){}
bool prepare_audio_pipeline(){g_left_tx=allocate();if(pipeline_fail==1)return false;g_right_tx=allocate();return pipeline_fail!=2;}
void reset_audio_control(){}
}
namespace lunaredu::lunaros {
bool create_managed_task(const ManagedTaskDefinition &d,ManagedTaskFunction,void*,ManagedTaskHandle*h,std::string&) {
 assert(std::strcmp(d.owner_service_id,"speaker.audio")==0);
 if(task_fail)return false;
 registered=true;*h=reinterpret_cast<ManagedTaskHandle>(uintptr_t(1));return true;
}
ManagedTaskRegistrySnapshot managed_task_registry_snapshot(){ManagedTaskRegistrySnapshot s;if(registered){s.tasks[0].active=true;strcpy(s.tasks[0].id.data(),"speaker_i2s");}return s;}
}
int main(){
 for(int failure=1;failure<=4;++failure){allocation=0;fail_allocation=failure;assert(!setup_dual_i2s_audio());assert(live.empty());}
 fail_allocation=0;
 for(int failure=1;failure<=2;++failure){pipeline_fail=failure;assert(!setup_dual_i2s_audio());assert(live.empty());}
 pipeline_fail=0;task_fail=true;assert(!setup_dual_i2s_audio());assert(live.empty());task_fail=false;
 assert(setup_dual_i2s_audio());const auto owned=live.size();assert(setup_dual_i2s_audio());assert(live.size()==owned);
 task_stuck=true;assert(!stop_dual_i2s_audio());assert(live.size()==owned&&g_output_task);
 task_stuck=false;release_fail=true;assert(!stop_dual_i2s_audio());assert(g_left_tx&&g_right_tx&&g_free_queue);assert(!setup_dual_i2s_audio());
 release_fail=false;assert(stop_dual_i2s_audio());assert(live.empty());assert(stop_dual_i2s_audio());
 assert(setup_dual_i2s_audio());assert(stop_dual_i2s_audio());assert(live.empty());
}
