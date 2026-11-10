#include "kernel/periodic_worker.h"
#include "kernel/runtime.h"
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <algorithm>
namespace lunaredu::lunaros {
bool PeriodicWorker::start(KernelRuntime &runtime, const ManagedTaskDefinition &task,
    PeriodicWorkerPorts ports, std::string &error) {
  if (runtime_ || !task.id || std::strlen(task.id)>=id_.size() ||
      !ports.step || !ports.period_us || !ports.period_us(ports.context)) {
    error="periodic_worker_configuration_invalid";return false;
  }
  runtime_=&runtime;ports_=ports;std::strcpy(id_.data(),task.id);
  stop_requested_.store(false);started_.store(false);released_at_us_.store(0);
  startup_error_.clear();
  initialization_failed_.store(false);
  esp_timer_handle_t timer=nullptr;
  esp_timer_create_args_t args{};
  args.callback=notify;args.arg=this;args.name="os.periodic";
  args.dispatch_method=ESP_TIMER_TASK;
  auto code=esp_timer_create(&args,&timer);
  if(code!=ESP_OK){runtime_=nullptr;error=esp_err_to_name(code);return false;}
  timer_=timer;
  if(!create_managed_task(runtime,task,run,this,nullptr,error)) {
    esp_timer_delete(timer);timer_=nullptr;runtime_=nullptr;return false;
  }
  const auto beginning=xTaskGetTickCount();
  while(!started_.load(std::memory_order_acquire) && !stop_requested_.load(std::memory_order_acquire)) {
    if(xTaskGetTickCount()-beginning>=std::max<TickType_t>(1,pdMS_TO_TICKS(100))) {
      (void)stop(error);error="periodic_worker_start_pending";return false;
    }
    vTaskDelay(1);
  }
  if(!started_.load(std::memory_order_acquire)) {
    error=initialization_failed_.load(std::memory_order_acquire) && !startup_error_.empty()
        ?startup_error_:"periodic_worker_timer_start_failed";return false;
  }
  return true;
}
void PeriodicWorker::notify(void *context) {
  auto &self=*static_cast<PeriodicWorker *>(context);
  StaticLockGuard lock(self.delivery_mutex_);
  const auto handle=self.handle_.load(std::memory_order_acquire);
  if(handle && !self.stop_requested_.load(std::memory_order_acquire)) {
    const uint64_t now=static_cast<uint64_t>(esp_timer_get_time());
    const uint64_t expected=self.expected_release_us_.load(std::memory_order_acquire);
    const uint32_t period=self.period_us_.load(std::memory_order_acquire);
    // Preserve the oldest outstanding release when notifications coalesce;
    // replacing it with the newest one would hide missed deadlines.
    if(!self.released_at_us_.load(std::memory_order_acquire))
      self.released_at_us_.store(expected,std::memory_order_release);
    if(period) self.expected_release_us_.store(expected+((now>=expected?(now-expected)/period:0)+1)*period,
        std::memory_order_release);
    xTaskNotifyGive(handle);
  }
}
void PeriodicWorker::run(void *context) {
  auto &self=*static_cast<PeriodicWorker *>(context);
  auto timer=static_cast<esp_timer_handle_t>(self.timer_);
  self.handle_.store(xTaskGetCurrentTaskHandle(),std::memory_order_release);
  if(self.stop_requested_.load(std::memory_order_acquire)) {
    {StaticLockGuard lock(self.delivery_mutex_);self.handle_.store(nullptr,std::memory_order_release);}
    if(self.ports_.exited)self.ports_.exited(self.ports_.context);
    return;
  }
  if(self.ports_.initialize && !self.ports_.initialize(self.ports_.context,self.startup_error_)) {
    self.initialization_failed_.store(true,std::memory_order_release);
    self.stop_requested_.store(true,std::memory_order_release);
    {StaticLockGuard lock(self.delivery_mutex_);self.handle_.store(nullptr,std::memory_order_release);}
    if(self.ports_.exited)self.ports_.exited(self.ports_.context);
    return;
  }
  uint32_t period=self.ports_.period_us(self.ports_.context);
  self.period_us_.store(period);
  self.expected_release_us_.store(static_cast<uint64_t>(esp_timer_get_time())+period);
  if(esp_timer_start_periodic(timer,period)!=ESP_OK) self.stop_requested_.store(true);
  else if(!self.stop_requested_.load(std::memory_order_acquire)) {
    if(self.ports_.activated && !self.ports_.activated(self.ports_.context,self.startup_error_)) {
      self.initialization_failed_.store(true,std::memory_order_release);
      self.stop_requested_.store(true,std::memory_order_release);
    } else self.started_.store(true,std::memory_order_release);
  }
  while(!self.stop_requested_.load(std::memory_order_acquire)) {
    if(!ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(100))) continue;
    if(self.stop_requested_.load(std::memory_order_acquire)) break;
    uint64_t released=0;
    {StaticLockGuard lock(self.delivery_mutex_);
      released=self.released_at_us_.exchange(0,std::memory_order_acq_rel);}
    if(!released) continue; // A coalesced notification was already consumed.
    self.ports_.step(self.ports_.context,released,static_cast<uint64_t>(esp_timer_get_time()));
    const auto next=self.ports_.period_us(self.ports_.context);
    if(next && next!=period) {
      esp_timer_stop(timer);period=next;
      self.period_us_.store(period);
      self.expected_release_us_.store(static_cast<uint64_t>(esp_timer_get_time())+period);
      if(esp_timer_start_periodic(timer,period)!=ESP_OK) self.stop_requested_.store(true);
    }
  }
  esp_timer_stop(timer);
  { StaticLockGuard lock(self.delivery_mutex_); self.handle_.store(nullptr,std::memory_order_release); }
  if(self.ports_.exited)self.ports_.exited(self.ports_.context);
}
bool PeriodicWorker::stop(std::string &error) {
  if(!runtime_) {error.clear();return true;}
  stop_requested_.store(true,std::memory_order_release);
  { StaticLockGuard lock(delivery_mutex_);
  const auto handle=handle_.load(std::memory_order_acquire);
  // Retirement is owner-serialized; timer callbacks stop delivering before
  // the wrapper releases the task reservation.
  if(handle) xTaskNotifyGive(handle);
  }
  if(runtime_->task_active(id_.data())) {error="periodic_worker_stop_pending";return false;}
  if(timer_) {
    const auto code=esp_timer_delete(static_cast<esp_timer_handle_t>(timer_));
    if(code!=ESP_OK) {error=esp_err_to_name(code);return false;}
  }
  timer_=nullptr;runtime_=nullptr;error.clear();return true;
}
bool PeriodicWorker::running() const { return started_.load(std::memory_order_acquire) && handle_.load(std::memory_order_acquire)!=nullptr && !stop_requested_.load(std::memory_order_acquire); }
} // namespace lunaredu::lunaros
