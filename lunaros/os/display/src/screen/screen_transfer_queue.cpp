#include "display/transfer_queue.h"
#include "display/pipeline/damage_set.h"
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include "diagnostics/runtime_work_profile.h"
#ifdef ESP_PLATFORM
#include "kernel/runtime.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

namespace lunaredu::display {
namespace {
constexpr TickType_t kWait = pdMS_TO_TICKS(500);
}

bool ScreenTransferQueue::start(
    lunaros::KernelRuntime &runtime, ScreenHost &host,
    const lunaros::ManagedTaskDefinition &task, uint16_t *first,
    uint16_t *second, uint16_t width, uint16_t height,
    ScreenPixelFormat format, std::string &error) {
  if (ready() || !task.id || std::strlen(task.id)>=task_id_.size() || !host.ready() || !first || !second || first == second ||
      width == 0 || height == 0 || width != host.width() || height != host.height() ||
      (format != ScreenPixelFormat::Rgb565 && format != ScreenPixelFormat::Rgb565ByteSwapped) || task.lifetime != lunaros::ManagedTaskLifetime::Resident) {
    error = "invalid_screen_transfer_queue";
    return false;
  }
  auto *first_done = xSemaphoreCreateBinary();
  auto *second_done = xSemaphoreCreateBinary();
  auto *control_replies = xQueueCreate(1, sizeof(ControlReply));
  auto *queue = xQueueCreate(3, sizeof(Work));
  if (!first_done || !second_done || !control_replies || !queue) {
    if (first_done) vSemaphoreDelete(first_done);
    if (second_done) vSemaphoreDelete(second_done);
    if (control_replies) vQueueDelete(control_replies);
    if (queue) vQueueDelete(queue);
    error = "screen_transfer_queue_allocation_failed";
    return false;
  }
  host_ = &host;
  slots_[0].pixels = first;
  slots_[1].pixels = second;
  slots_[0].completed = first_done;
  slots_[1].completed = second_done;
  control_replies_ = control_replies;
  queue_ = queue;
  width_ = width;
  height_ = height;
  format_ = format;
  runtime_ = &runtime;
  std::memcpy(task_id_.data(),task.id,std::strlen(task.id)+1);
  finished_.store(false);stopping_=stop_queued_=false;
  if (!lunaros::create_managed_task(runtime, task, run, this, &task_, error)) {
    vQueueDelete(queue);
    vQueueDelete(control_replies);
    vSemaphoreDelete(second_done);
    vSemaphoreDelete(first_done);
    queue_ = control_replies_ = nullptr;
    slots_ = {};
    host_ = nullptr;
    return false;
  }
  error.clear();
  return true;
}

bool ScreenTransferQueue::settle_control(std::string &error) {
  if (!pending_request_) return true;
  ControlReply reply;
  if (xQueueReceive(static_cast<QueueHandle_t>(control_replies_), &reply, kWait) != pdTRUE) {
    ++control_timeouts_; pending_timed_out_=true;
    error = "screen_control_timeout"; return false;
  }
  if (reply.request != pending_request_) {
    ++mismatched_replies_;
    error = "screen_control_reply_mismatch"; return false;
  }
  pending_request_ = 0;
  observed_request_.store(0);
  if(pending_timed_out_)++late_replies_;
  pending_timed_out_=false;
  if (!reply.succeeded) { error = reply.error.data(); return false; }
  if (pending_command_ == Command::Acquire) active_ = true;
  if (pending_command_ == Command::Release) active_ = false;
  error.clear(); return true;
}

bool ScreenTransferQueue::dispatch(Command command, std::string &error, const char *owner) {
  if (!ready() || !settle_control(error) || !wait_all(error)) return false;
  Work work{command};
  if (++next_request_ == 0) ++next_request_;
  work.request = next_request_;
  if (owner) std::memcpy(work.owner.data(), owner, std::strlen(owner) + 1);
  if (xQueueSend(static_cast<QueueHandle_t>(queue_), &work, 0) != pdTRUE) {
    error = "screen_control_queue_full";
    return false;
  }
  pending_request_ = work.request;
  observed_request_.store(work.request);
  pending_timed_out_=false;
  pending_command_ = command;
  pending_owner_ = work.owner;
  return settle_control(error);
}

bool ScreenTransferQueue::acquire(const char *owner, std::string &error) {
  if(stopping_){error="screen_transfer_stopping";return false;}
  if (!owner || !owner[0] || std::strlen(owner) >= lunaros::kManagedTaskOwnerBytes) {
    error = "screen_transfer_owner_invalid"; return false;
  }
  const bool completing_acquire = pending_request_ && pending_command_ == Command::Acquire &&
      std::strcmp(owner, pending_owner_.data()) == 0;
  if (!settle_control(error)) return false;
  if (active()) {
    if (completing_acquire) { error.clear(); return true; }
    error = "screen_transfer_owner_invalid"; return false;
  }
  return dispatch(Command::Acquire, error, owner);
}

bool ScreenTransferQueue::present(unsigned buffer, const ScreenRegion *regions,
                                  size_t count, std::string &error, FrameCanvas canvas) {
  if(stopping_){error="screen_transfer_stopping";return false;}
  if (!settle_control(error)) return false;
  if (!active_ || buffer >= slots_.size() || !regions || count == 0 ||
      count > kScreenBatchMaxRegions) {
    error = "screen_transfer_batch_invalid";
    return false;
  }
  if (!wait(buffer, error)) return false;
  auto &slot = slots_[buffer];
  if(canvas.issuer) {
    const auto base=reinterpret_cast<uintptr_t>(slot.pixels),address=reinterpret_cast<uintptr_t>(canvas.pixels);
    const size_t bytes=size_t(width_)*height_*sizeof(uint16_t);
    const size_t used=(size_t(canvas.height?canvas.height-1:0)*canvas.stride+canvas.width)*sizeof(uint16_t);
    if(canvas.slot != buffer || !canvas.width || !canvas.height || canvas.width>width_ || canvas.height>height_ ||
        canvas.stride!=width_ || !canvas.generation || address<base || address-base>=bytes || used>bytes-(address-base)) {
      error="screen_transfer_canvas_invalid";return false;
    }
  }
  slot.canvas=canvas;
  for (size_t i = 0; i < count; ++i) {
    const auto &region = regions[i];
    if (region.x < 0 || region.y < 0 || region.width <= 0 ||
        region.height <= 0 || region.x > width_ || region.y > height_ ||
        region.width > width_ - region.x || region.height > height_ - region.y) {
      error = "screen_transfer_region_invalid";
      return false;
    }
    slot.regions[i] = region;
  }
  slot.region_count = count;
  Work work{Command::Present, static_cast<uint8_t>(buffer)};
  if(system::worker_work_profiling_active())work.queued_us=esp_timer_get_time();
  if (xQueueSend(static_cast<QueueHandle_t>(queue_), &work, 0) != pdTRUE) {
    error = "screen_transfer_queue_full";
    return false;
  }
  slot.in_flight = true;
  error.clear();
  return true;
}

bool ScreenTransferQueue::wait(unsigned buffer, std::string &error) {
  if (buffer >= slots_.size()) {
    error = "screen_transfer_buffer_invalid";
    return false;
  }
  auto &slot = slots_[buffer];
  if (!slot.in_flight) return true;
  if (xSemaphoreTake(static_cast<SemaphoreHandle_t>(slot.completed), kWait) !=
      pdTRUE) {
    ++frame_timeouts_;
    error = "screen_transfer_timeout";
    return false;
  }
  slot.in_flight = false;
  if (!slot.succeeded) {
    error = slot.error;
    return false;
  }
  error.clear();
  return true;
}

bool ScreenTransferQueue::wait_all(std::string &error) {
  bool ok = true;
  for (unsigned i = 0; i < slots_.size(); ++i) {
    std::string detail;
    if (!wait(i, detail)) {
      ok = false;
      if (error.empty()) error = detail;
    }
  }
  return ok;
}

bool ScreenTransferQueue::drain(std::string &error) {
  if (!settle_control(error)) return false;
  if (!active_) { error = "screen_transfer_owner_invalid"; return false; }
  return dispatch(Command::Drain, error);
}

bool ScreenTransferQueue::release(std::string &error) {
  if (!settle_control(error)) return false;
  if (!active_) return true;
  return dispatch(Command::Release, error);
}

void ScreenTransferQueue::run(void *context) {
  static_cast<ScreenTransferQueue *>(context)->worker();
}

void ScreenTransferQueue::worker() {
  for (;;) {
    Work work{};
    if (xQueueReceive(static_cast<QueueHandle_t>(queue_), &work,
                      portMAX_DELAY) != pdTRUE)
      continue;
    process(work);
    if(work.command==Command::Stop)break;
  }
  // Last owner access. Core retires the task registration after run returns.
  finished_.store(true,std::memory_order_release);
}

void ScreenTransferQueue::process(Work work) {
    std::string error;
    bool succeeded = false;
    switch (work.command) {
      case Command::Acquire:
        succeeded = host_->acquire(work.owner.data(), lease_, error);
        break;
      case Command::Present: {
        auto &profile=present_profile_;
        auto &queue_us=queued_profile_us_;
        auto &max_queue_us=max_queued_profile_us_;
        if(!system::worker_work_profiling_active()){
          queue_us=max_queue_us=profile_bytes_=0;
          profile_regions_=profile_completed_=profile_failed_=0;
        }
        profile.begin();
        if(profile.fresh_window()){
          queue_us=max_queue_us=profile_bytes_=0;
          profile_regions_=profile_completed_=profile_failed_=0;
        }
        if(work.queued_us){const auto age=uint64_t(esp_timer_get_time())-work.queued_us;queue_us+=age;max_queue_us=std::max(max_queue_us,age);}
        auto &slot = slots_[work.buffer];
        succeeded = true;
        for (size_t i = 0; i < slot.region_count; ++i) {
          if(profile.active()){
            ++profile_regions_;
            profile_bytes_+=uint64_t(slot.regions[i].width)*slot.regions[i].height*2;
          }
          if (!host_->present(lease_,
                  {slot.pixels, width_, height_, width_, format_},
                  slot.regions[i], error)) {
            succeeded = false;
            break;
          }
        }
        profile.mark(0);
        if(succeeded)succeeded=host_->wait_idle(lease_,error);
        profile.mark(1);
        if(succeeded)host_->frame_presented(lease_,{slot.pixels,width_,height_,width_,format_,
            slot.canvas.issuer,slot.canvas.generation,work.buffer},slot.regions.data(),slot.region_count);
        slot.error = error;
        slot.succeeded = succeeded;
        if(profile.active()){if(succeeded)++profile_completed_;else ++profile_failed_;}
        xSemaphoreGive(static_cast<SemaphoreHandle_t>(slot.completed));
        profile.mark(2);
        const auto measured=profile.finish();
        if(measured.calls){
          ESP_LOGI("screen.transfer","DISPLAY_PROFILE calls=%u completed_batches=%u failed_batches=%u regions=%u bytes=%llu window_us=%llu queue_us=%llu max_queue_us=%llu present_us=%llu drain_us=%llu receipt_us=%llu max_present_us=%u worker_task_us=%u",
              unsigned(measured.calls),unsigned(profile_completed_),unsigned(profile_failed_),unsigned(profile_regions_),static_cast<unsigned long long>(profile_bytes_),
              static_cast<unsigned long long>(measured.window_us),static_cast<unsigned long long>(queue_us),static_cast<unsigned long long>(max_queue_us),
              static_cast<unsigned long long>(measured.wall[0]),static_cast<unsigned long long>(measured.wall[1]),static_cast<unsigned long long>(measured.wall[2]),unsigned(measured.maximum_us[0]),unsigned(measured.worker_task_us));
          queue_us=max_queue_us=profile_bytes_=0;
          profile_regions_=profile_completed_=profile_failed_=0;
        }
        break;
      }
      case Command::Release:
        succeeded = host_->release(lease_, error);
        if (succeeded) lease_ = {};
        break;
      case Command::Drain:
        succeeded = host_->wait_idle(lease_, error);
        break;
      case Command::Stop:
        succeeded = true;
        break;
    }
    if (work.command != Command::Present) {
      ControlReply reply;
      reply.request = work.request; reply.succeeded = succeeded;
      const auto length = std::min(error.size(), reply.error.size() - 1);
      std::memcpy(reply.error.data(), error.data(), length);
      // There is exactly one outstanding control request, including timeouts.
      (void)xQueueSend(static_cast<QueueHandle_t>(control_replies_), &reply, portMAX_DELAY);
    }
}
bool ScreenTransferQueue::stop(std::string &error) {
  if(!ready()){error.clear();return true;}
  stopping_=true;
  if(!stop_queued_) {
    if(!release(error))return false; // Includes pending control and frame completion.
    const bool queued=dispatch(Command::Stop,error);
    stop_queued_=queued||(pending_request_&&pending_command_==Command::Stop);
    if(!queued)return false;
  } else if(!settle_control(error))return false;
  unsigned waits=0;
  while(!finished_.load(std::memory_order_acquire)||runtime_->task_active(task_id_.data())) {
    if(++waits>500){error="screen_transfer_stop_timeout";return false;}
    vTaskDelay(std::max<TickType_t>(1,pdMS_TO_TICKS(1)));
  }
  vQueueDelete(static_cast<QueueHandle_t>(queue_));
  vQueueDelete(static_cast<QueueHandle_t>(control_replies_));
  for(auto &slot:slots_)vSemaphoreDelete(static_cast<SemaphoreHandle_t>(slot.completed));
  slots_={};queue_=control_replies_=nullptr;task_=nullptr;runtime_=nullptr;host_=nullptr;
  active_.store(false);lease_={};error.clear();return true;
}
} // namespace lunaredu::display

#else
namespace lunaredu::display {
bool ScreenTransferQueue::stop(std::string &error) { error.clear(); return true; }
bool ScreenTransferQueue::start(lunaros::KernelRuntime &, ScreenHost &,
    const lunaros::ManagedTaskDefinition &, uint16_t *, uint16_t *, uint16_t,
    uint16_t, ScreenPixelFormat, std::string &error) {
  error = "screen_transfer_queue_requires_esp";
  return false;
}
bool ScreenTransferQueue::acquire(const char *, std::string &error) {
  error = "screen_transfer_queue_requires_esp"; return false;
}
bool ScreenTransferQueue::present(unsigned, const ScreenRegion *, size_t,
                                  std::string &error, FrameCanvas) {
  error = "screen_transfer_queue_requires_esp"; return false;
}
bool ScreenTransferQueue::wait(unsigned, std::string &error) {
  error = "screen_transfer_queue_requires_esp"; return false;
}
bool ScreenTransferQueue::wait_all(std::string &error) {
  error = "screen_transfer_queue_requires_esp"; return false;
}
bool ScreenTransferQueue::release(std::string &error) {
  error = "screen_transfer_queue_requires_esp"; return false;
}
bool ScreenTransferQueue::drain(std::string &error) {
  error = "screen_transfer_queue_requires_esp"; return false;
}
}
#endif
