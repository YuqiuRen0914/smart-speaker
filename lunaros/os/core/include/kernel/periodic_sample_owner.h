#pragma once
#include "kernel/managed_task.h"
#include "kernel/periodic.h"
#include "kernel/sample_channel.h"
#include "kernel/rate_schedule.h"
#include <atomic>
#include <algorithm>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace lunaredu::lunaros {
// A retained sampler on one declared managed worker. Hardware sampling and its
// order stay with the capability provider; OS owns timing, publication and the
// checked pause acknowledgement. pause never deletes a task or frees its owner.
template<class T> class PeriodicSampleOwner {
public:
  bool start(KernelRuntime &runtime, ManagedTaskDefinition task, void *context,
             bool (*sample)(void *, T &), int64_t (*clock_us)(),
             uint32_t period_ms, std::string &error) {
    if (ready_) {
      if (context != context_ || sample != sample_ || clock_us != clock_) {
        error = "sampler_owner_mismatch"; return false;
      }
      error.clear(); return true;
    }
    if (!sample || !clock_us || !period_ms || period_ms >= 0x80000000U) {
      error = "sampler_configuration_invalid"; return false;
    }
    context_ = context; sample_ = sample; clock_ = clock_us;
    set_period_ms(period_ms);
    ready_ = create_managed_task(runtime, task, run, this, nullptr, error);
    return ready_;
  }
  bool pause(uint32_t timeout_ms, std::string &error) {
    // pause/resume are serialized on the capability owner. Each fresh request
    // has its own acknowledgement so an earlier pause cannot satisfy it.
    if (!pause_requested_.load(std::memory_order_acquire)) {
      pause_generation_.fetch_add(1, std::memory_order_acq_rel);
      pause_requested_.store(true, std::memory_order_release);
      notify();
    }
    const auto generation = pause_generation_.load(std::memory_order_acquire);
    const auto started = xTaskGetTickCount();
    while (ready_ && acknowledged_generation_.load(std::memory_order_acquire) != generation &&
           xTaskGetTickCount() - started < pdMS_TO_TICKS(timeout_ms)) vTaskDelay(1);
    if (ready_ && acknowledged_generation_.load(std::memory_order_acquire) != generation) {
      error = "sampler_suspend_timeout"; return false;
    }
    error.clear(); return true;
  }
  void resume() { pause_requested_.store(false, std::memory_order_release); notify(); }
  bool pause_requested() const { return pause_requested_.load(std::memory_order_acquire); }
  bool ready() const { return ready_; }
  void discard() { samples_.publish({}); }
  void set_period_ms(uint32_t ms) {
    const auto old_rate=rate_hz_.exchange(0,std::memory_order_acq_rel);
    const auto ticks = pdMS_TO_TICKS(ms);
    if(period_.exchange(ticks?ticks:1,std::memory_order_acq_rel)!=(ticks?ticks:1)||old_rate)notify();
  }
  void set_rate_hz(uint32_t hz) {
    hz=hz?std::min<uint32_t>(hz,1000):1;
    if(rate_hz_.exchange(hz,std::memory_order_acq_rel)!=hz)notify();
  }
  uint32_t rate_hz() const {const auto hz=rate_hz_.load();return hz?hz:1000/period_ms();}
  uint32_t period_ms() const {const auto hz=rate_hz_.load();return hz?(1000+hz-1)/hz:period_.load(std::memory_order_acquire)*portTICK_PERIOD_MS;}
  SampleSnapshot<T> snapshot() const { return samples_.snapshot(); }
private:
  static void run(void *context) {
    auto &self = *static_cast<PeriodicSampleOwner *>(context);
    self.task_.store(xTaskGetCurrentTaskHandle(),std::memory_order_release);
    TickType_t wake = xTaskGetTickCount();
    RateSchedule cadence;
    uint32_t sequence = 0;
    for (;;) {
      if (self.pause_requested_.load(std::memory_order_acquire)) {
        self.acknowledged_generation_.store(
            self.pause_generation_.load(std::memory_order_acquire), std::memory_order_release);
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY); wake=xTaskGetTickCount();cadence.reset();continue;
      }
      const auto started=uint32_t(self.clock_()/1000);
      SampleSnapshot<T> frame;
      frame.sequence = ++sequence;
      frame.valid = self.sample_(self.context_, frame.value);
      frame.sampled_at_us = self.clock_();
      self.samples_.publish(frame);
      const auto hz=self.rate_hz_.load(std::memory_order_acquire);
      if(hz){
        cadence.complete(started,uint32_t(frame.sampled_at_us/1000),hz);
        const auto delay=cadence.wait_ms(uint32_t(self.clock_()/1000),hz);
        ulTaskNotifyTake(pdTRUE,std::max<TickType_t>(1,pdMS_TO_TICKS(delay)));
        continue;
      }
      const auto period = self.period_.load(std::memory_order_acquire);
      wake = periodic_delay_base(xTaskGetTickCount(), wake, period, 1);
      vTaskDelayUntil(&wake, period);
    }
  }
  SampleChannel<T> samples_;
  void notify(){if(const auto task=task_.load(std::memory_order_acquire))xTaskNotifyGive(task);}
  void *context_ = nullptr;
  bool (*sample_)(void *, T &) = nullptr;
  int64_t (*clock_)() = nullptr;
  std::atomic<TickType_t> period_{pdMS_TO_TICKS(10)};
  std::atomic<uint32_t> rate_hz_{0};
  std::atomic<TaskHandle_t> task_{nullptr};
  std::atomic<bool> pause_requested_{false};
  std::atomic<uint32_t> pause_generation_{0}, acknowledged_generation_{0};
  bool ready_ = false;
};
}
