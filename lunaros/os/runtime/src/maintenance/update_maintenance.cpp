#include "maintenance/update_maintenance.h"
#include "startup/product_runtime_internal.h"
#include "runtime/device_context.h"
#include "kernel/runtime.h"
#include "lunaredu_ota.h"
#include "lunaros_network.h"
#include "lunaros_network_jobs.h"
#include "lunaros_resource_managed_assets.h"
#include <cstring>
#include <algorithm>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace lunaredu::lunaros {
bool RuntimeUpdateMaintenance::configure(UpdateMaintenanceDefinition definition,std::string &error){
  if(configured_){error="update_maintenance_already_configured";return false;}
  if(definition.participants.size()>participants_.size()||!definition.timeout_ms){error="update_maintenance_definition_invalid";return false;}
  for(size_t i=0;i<definition.participants.size();++i){const auto &p=definition.participants[i];
    if(!p.id||!*p.id||!p.executor||!*p.executor||!p.step){error="update_maintenance_participant_invalid";return false;}
    for(size_t j=0;j<i;++j)if(!std::strcmp(p.id,definition.participants[j].id)){error="update_maintenance_participant_duplicate";return false;}
  }
  definition_=definition;configured_=true;
  ota_service_set_maintenance_callback([](bool entering,std::string &error,void *p){return static_cast<RuntimeUpdateMaintenance *>(p)->request(entering,error);},this);
  // Lifecycle notifications only revoke admission or enqueue recovery. All
  // physical operations and foreground mutation remain executor-owned.
  ota_service_set_lifecycle_callback([](OtaServiceLifecycleEvent event,void *p){
    auto &self=*static_cast<RuntimeUpdateMaintenance *>(p);
    if(event==OtaServiceLifecycleEvent::UploadAborted){
      if(self.active())self.phase_.store(3,std::memory_order_release);
    }
  },this);
  error.clear();return true;
}
bool RuntimeUpdateMaintenance::all(bool entering,uint32_t epoch) const {
  for(size_t i=0;i<definition_.participants.size();++i)
    if((entering?participants_[i].entered:participants_[i].released).load(std::memory_order_acquire)!=epoch)return false;
  return true;
}
void RuntimeUpdateMaintenance::poll(const char *executor){
  const auto phase=phase_.load(std::memory_order_acquire);
  if(!phase||!executor||!device_.runtime.current_task_owns_executor(executor))return;
  const auto epoch=epoch_.load(std::memory_order_acquire);
  const bool main=!std::strcmp(executor,kMainServiceExecutor);
  if(phase<3){
    for(size_t i=0;i<definition_.participants.size();++i){const auto &definition=definition_.participants[i];auto &state=participants_[i];
      if(std::strcmp(executor,definition.executor)||state.revoked.load(std::memory_order_acquire)==epoch)continue;
      if(definition.revoke)definition.revoke(definition.context,epoch);
      state.revoked.store(epoch,std::memory_order_release);
    }
  }
  if(phase<3&&main&&!main_quiesced_.load(std::memory_order_acquire)){
    std::string error;
    if(!foreground_.quiesce(device_.application(),"firmware_update",nullptr,error))return;
    if(definition_.retire_background&&!device_.runtime.reserve_service_resource(kBackgroundExecutionResource,"runtime.ota",background_,error))return;
    main_quiesced_.store(true,std::memory_order_release);
  }
  for(size_t i=0;i<definition_.participants.size();++i){const auto &definition=definition_.participants[i];auto &state=participants_[i];
    if(std::strcmp(executor,definition.executor))continue;
    if(phase<3){
      // A Main-owned input release during foreground exit cannot undo the
      // final physical pause: perform participant work after that exit.
      if(main&&!main_quiesced_.load(std::memory_order_acquire))continue;
      if(state.entered.load(std::memory_order_acquire)==epoch)continue;
      state.touched=true;
      if(definition.step(definition.context,true,epoch,state.error))state.entered.store(epoch,std::memory_order_release);
    }else if(state.released.load(std::memory_order_acquire)!=epoch){
      if(!state.touched||definition.step(definition.context,false,epoch,state.error)){
        state.touched=false;state.released.store(epoch,std::memory_order_release);
      }
    }
  }
  if(!main)return;
  if(phase<3){
    if(main_quiesced_.load(std::memory_order_acquire)&&all(true,epoch)&&managed_asset_operations_idle()){
      unsigned expected=1;phase_.compare_exchange_strong(expected,2,std::memory_order_acq_rel);
    }
    return;
  }
  if(!all(false,epoch))return;
  std::string error;
  if(background_.generation&&!device_.runtime.release_service_resource(background_,error))return;
  if(!admission_released_){
    if(definition_.admission)definition_.admission(definition_.context,false,epoch);
    admission_released_=true;
  }
  if(!foreground_.restore(device_.application(),error))return;
  main_quiesced_.store(false,std::memory_order_release);
  phase_.store(0,std::memory_order_release);
}
bool RuntimeUpdateMaintenance::request(bool entering,std::string &error){
  std::lock_guard<std::mutex> request(request_mutex_);
  if(!entering){if(active())phase_.store(3,std::memory_order_release);error.clear();return !active();}
  if(phase_.load(std::memory_order_acquire)==3){error="ota_restore_pending";return false;}
  if(!active()){
    auto next=epoch_.fetch_add(1,std::memory_order_acq_rel)+1;if(!next){epoch_.store(1,std::memory_order_release);next=1;}
    admission_released_=false;main_quiesced_.store(false,std::memory_order_release);
    if(definition_.admission)definition_.admission(definition_.context,true,next);
    phase_.store(1,std::memory_order_release);
  }
  const auto deadline=esp_timer_get_time()+int64_t(definition_.timeout_ms)*1000;
  while(!quiesced()){
    if(phase_.load(std::memory_order_acquire)==3){error="ota_maintenance_cancelled";return false;}
    if(device_.runtime.current_task_owns_executor(kMainServiceExecutor))poll(kMainServiceExecutor);
    if(esp_timer_get_time()>=deadline){error="ota_product_quiescence_timeout";phase_.store(3,std::memory_order_release);return false;}
    (void)poll_network_ota_retirement(uint32_t(esp_timer_get_time()/1000));
    vTaskDelay(std::max<TickType_t>(1,pdMS_TO_TICKS(5)));
  }
  error.clear();return true;
}
bool configure_update_maintenance(DeviceContext &device,UpdateMaintenanceDefinition definition,std::string &error){return ProductRuntimeAccess::configure_update(device,definition,error);}
void poll_update_maintenance(DeviceContext &device,const char *executor){if(auto *owner=ProductRuntimeAccess::update_maintenance(device))owner->poll(executor);}
bool update_maintenance_blocked(const DeviceContext &device){const auto *owner=ProductRuntimeAccess::update_maintenance(device);return owner&&owner->blocked();}
bool update_maintenance_active(const DeviceContext &device){const auto *owner=ProductRuntimeAccess::update_maintenance(device);return owner&&owner->active();}
bool update_maintenance_quiesced(const DeviceContext &device){const auto *owner=ProductRuntimeAccess::update_maintenance(device);return owner&&owner->quiesced();}
uint32_t update_maintenance_epoch(const DeviceContext &device){const auto *owner=ProductRuntimeAccess::update_maintenance(device);return owner?owner->epoch():0;}
}
