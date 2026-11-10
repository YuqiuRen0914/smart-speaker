#include "diagnostics/function_probe.h"
#pragma once
// Included by managed_screen.h after its declarations. Not a consumer entry.
#include <algorithm>
#include <cstdlib>
#ifndef LUNAROS_FRESH_FRAME_HANDOFF
#define LUNAROS_FRESH_FRAME_HANDOFF 1
#endif
#ifdef ESP_PLATFORM
#include "kernel/task_signal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#endif

namespace lunaredu::display {
template<class Job>
ManagedScreen<Job>::ManagedScreen(uint64_t (*clock)(), RenderDispatch dispatch)
    : session_(clock), clock_(clock), dispatch_(dispatch) {}

template<class Job>
ManagedScreen<Job>::~ManagedScreen() {
  if (session_.retained()) std::abort();
  std::string error;
  if (!stop(error)) std::abort();
}

template<class Job>
bool ManagedScreen<Job>::render_service(RenderServicePorts ports) {
  if (lifecycle() != ScreenLifecycle::Unconfigured) return false;
  service_ = ports; return true;
}

template<class Job>
bool ManagedScreen<Job>::observe_transfer(void *context, void (*observer)(void *, ScreenPatchView, ScreenRegion)) {
  if (lifecycle() != ScreenLifecycle::Unconfigured) return false;
  observer_context_ = context; observer_ = observer; return true;
}

template<class Job>
bool ManagedScreen<Job>::observe_presented(void *context,void (*observer)(void *,ScreenFrameView,const ScreenRegion *,size_t)){
  if(lifecycle()!=ScreenLifecycle::Unconfigured)return false;
  presented_context_=context;presented_observer_=observer;return true;
}

template<class Job>
bool ManagedScreen<Job>::observe_unchanged(void *context, void (*observer)(void *, FrameCanvas)) {
  if (lifecycle() != ScreenLifecycle::Unconfigured) return false;
  unchanged_context_ = context; unchanged_observer_ = observer; return true;
}

template<class Job>
bool ManagedScreen<Job>::setup(lunaros::KernelRuntime *runtime, ScreenBackend backend,
    const char *owner, ScreenPipelineTasks tasks, Paint paint, void *context,
    std::string &error, unsigned auxiliary_frames,
    ScreenExecution execution, ScreenStorageBudget budget) {
  if (lifecycle() != ScreenLifecycle::Unconfigured) { error = "screen_already_configured"; return false; }
  if (!paint || !clock_ || !backend.present_patch) { error = "screen_ports_invalid"; return false; }
#ifdef ESP_PLATFORM
  if (!runtime) { error = "screen_runtime_missing"; return false; }
#endif
  paint_ = paint; context_ = context;
  backend_ = backend;
  backend.context = this;
  backend.present = [](void *p, ScreenFrameView frame, ScreenRegion region, std::string &error) {
    return static_cast<ManagedScreen *>(p)->transfer(
        {frame.pixels+region.y*frame.stride+region.x,region.width,region.height,frame.stride,frame.format,true},region,error);
  };
  backend.present_patch = [](void *p, ScreenPatchView patch, ScreenRegion region, std::string &error) {
    return static_cast<ManagedScreen *>(p)->transfer(patch,region,error);
  };
  backend.frame_presented=[](void *p,ScreenFrameView frame,const ScreenRegion *regions,size_t count){
    auto &self=*static_cast<ManagedScreen *>(p);
    if(self.backend_.frame_presented)self.backend_.frame_presented(self.backend_.context,frame,regions,count);
    if(self.presented_observer_)self.presented_observer_(self.presented_context_,frame,regions,count);
  };
  backend.wait_idle = [](void *p, std::string &error) {
    auto &self=*static_cast<ManagedScreen *>(p);
    return !self.backend_.wait_idle || self.backend_.wait_idle(self.backend_.context,error);
  };
  if (!session_.prepare(backend, error, external_screen_memory(), auxiliary_frames, budget, sizeof(detail::RenderMailbox<Job>))) return false;
  mailbox_ = new(session_.mailbox_storage_) detail::RenderMailbox<Job>();
  session_.destroy_mailbox_ = [](void *p){static_cast<detail::RenderMailbox<Job> *>(p)->~RenderMailbox();};
#ifdef ESP_PLATFORM
  caller_execution_ = execution == ScreenExecution::Caller;
  if (caller_execution_) ready_ = session_.start_direct(owner, error);
  else ready_ = session_.start(*runtime, owner, tasks, [](void *p) {
    auto &self = *static_cast<ManagedScreen *>(p);
    uint64_t next_stack_sample=0;
    while (!self.stop_requested_.load()) {
      const auto sample_now=self.clock_();
      if(sample_now>=next_stack_sample){self.session_.sample_renderer_stack();next_stack_sample=sample_now+1'000'000;}
      const auto delay = self.service_.next_wake_ms ? self.service_.next_wake_ms(self.service_.context) : UINT32_MAX;
      lunaredu::lunaros::task_signal::wait_ms(delay == UINT32_MAX ? delay : std::max<uint32_t>(1,delay));
      if(self.stop_requested_.load())break;
      while (!self.stop_requested_.load() && self.render_next()) { self.service_timed(); vTaskDelay(1); }
      self.service_timed();
    }
    self.session_.sample_renderer_stack();
    self.renderer_finished_.store(true,std::memory_order_release); // Last owner access.
  }, this, {this,[](void *p){static_cast<ManagedScreen *>(p)->stop_requested_.store(true);},
      [](void *p){return static_cast<ManagedScreen *>(p)->renderer_finished_.load(std::memory_order_acquire);}}, error);
#else
  (void)runtime; (void)tasks; (void)execution;
  caller_execution_ = true;
  ready_ = session_.start_direct(owner, error);
#endif
  if(!session_.width())mailbox_=nullptr;
  if (ready_ && backend_.bind_redraw)
    backend_.bind_redraw(backend_.context,this,[](void *p){
      auto &screen=*static_cast<ManagedScreen *>(p);screen.full_redraw_.store(true);screen.request_redraw();
    });
  return ready_;
}

template<class Job>
template<class Capture, class Coalesce>
void ManagedScreen<Job>::publish(Capture capture, Coalesce coalesce) {
  std::unique_lock<std::mutex> admission(producer_mutex_,std::try_to_lock);
  if(!admission.owns_lock())return;
  if (!ready_ || stop_requested_.load() || paused_.load()) return;
  auto &job = mailbox_->begin(); capture(job);
  mailbox_->publish([&](Job &pending){++coalesced_jobs_;coalesce(pending);});
  admission.unlock();wake();
}

template<class Job>
bool ManagedScreen<Job>::bind_authority(ScreenAuthority authority,const char *output,std::string &error) {
  if(lifecycle()!=ScreenLifecycle::Unconfigured){error="screen_already_configured";return false;}
  return session_.bind_authority(authority,output,error);
}
template<class Job>
bool ManagedScreen<Job>::stop(std::string &error) {
  bool expected=false;
  if(!retiring_.compare_exchange_strong(expected,true)){error="screen_stop_in_progress";return false;}
  struct Retirement {std::atomic<bool> &gate;~Retirement(){gate.store(false);}} retirement{retiring_};
  std::unique_lock<std::mutex> admission(producer_mutex_,std::try_to_lock);
  if(!admission.owns_lock()){error="screen_render_pending";return false;}
  RenderGuard render(render_busy_);
  if(!render){error="screen_render_pending";return false;}
  // A caller may be inside capture/paint. It must finish before storage retires.
  if(mailbox_ && (mailbox_->writing() || (caller_execution_ && mailbox_->busy()))) {error="screen_render_pending";return false;}
  stop_requested_.store(true);
  if(mailbox_)mailbox_->cancel_pending();
  // Admission is closed. Let a wake already waiting for this lock observe
  // the stop flag instead of blocking the renderer's retirement behind us.
  admission.unlock();
  if(!session_.stop(error))return false;
  if(ready_ && backend_.bind_redraw)backend_.bind_redraw(backend_.context,nullptr,nullptr);
  ready_=false;mailbox_=nullptr;backend_={};return true;
}
template<class Job>
bool ManagedScreen<Job>::quiesce(std::string &error) {
  paused_.store(true,std::memory_order_release);
  std::unique_lock<std::mutex> admission(producer_mutex_,std::try_to_lock);
  if(!admission.owns_lock()){error="screen_render_pending";return false;}
  RenderGuard render(render_busy_);
  if(!render){error="screen_render_pending";return false;}
  if(mailbox_)mailbox_->cancel_pending();
  if(lifecycle()==ScreenLifecycle::Unconfigured || lifecycle()==ScreenLifecycle::Stopped) {
    error.clear();return true;
  }
  // No producer, paint or render-service callback can submit after this drain.
  if(!session_.wait_idle(error))return false;
  session_.borrowed_={}; // Pre-maintenance canvases cannot be replayed after resume.
  session_.invalidate_frame();
  error.clear();return true;
}
template<class Job>
void ManagedScreen<Job>::resume() {
  if(!paused_.exchange(false,std::memory_order_acq_rel))return;
  full_redraw_.store(true);request_redraw();wake();
}
template<class Job>
bool ManagedScreen<Job>::ready() const { return ready_; }

template<class Job>
ScreenLifecycle ManagedScreen<Job>::lifecycle() const { return session_.lifecycle(); }

template<class Job>
ScreenSessionSnapshot ManagedScreen<Job>::snapshot() const {
  auto value=session_.snapshot();
  value.coalesced_jobs=coalesced_jobs_.load();value.pacing_wait_us=pacing_wait_us_.load();
  value.render_budget_misses=render_budget_misses_.load();value.transfer_budget_misses=transfer_budget_misses_.load();
  return value;
}

template<class Job>
uint64_t ManagedScreen<Job>::now_us() const {return clock_();}

template<class Job>
template<class Work>
decltype(auto) ManagedScreen<Job>::timed(ScreenPipelinePhase phase,Work work) {
  ScreenPipelineTiming timing{session_.metrics_,phase,clock_};return work();
}

template<class Job>
uint16_t ManagedScreen<Job>::width() const {return session_.width();}

template<class Job>
uint16_t ManagedScreen<Job>::height() const {return session_.height();}

template<class Job>
FrameCanvas ManagedScreen<Job>::begin_frame(uint16_t width,uint16_t height,uint16_t color,uint16_t y,bool clear,std::string &error) {
  if(paused_.load()){error="screen_paused";return {};}
  if(lifecycle()==ScreenLifecycle::Stopped || !session_.width()){error="screen_not_running";return {};}
  if(!pace_frame()){error="screen_stopping";return {};}
  auto frame=session_.begin_frame(width,height,color,y,clear,error);
  if(frame.pixels)frame_cadence_.began(clock_());
  else log_failure("begin",error);
  return frame;
}

template<class Job>
bool ManagedScreen<Job>::pace_frame() {
#ifdef ESP_PLATFORM
  // Keep pending snapshots replaceable while waiting. Only the render owner
  // sleeps; no input tick, borrowed frame or active mailbox slot is held.
  const auto before=clock_();
  while(!stop_requested_.load()) {
    const auto delay=frame_cadence_.delay_us(clock_());
    if(!delay)break;
    vTaskDelay(std::max<TickType_t>(1,pdMS_TO_TICKS((delay+999)/1000)));
  }
  pacing_wait_us_.fetch_add(clock_()-before);
#endif
  return !stop_requested_.load();
}

template<class Job>
uint16_t *ManagedScreen<Job>::active_frame() const {return session_.active_frame();}

template<class Job>
unsigned ManagedScreen<Job>::active_slot() const {return session_.active_slot();}

template<class Job>
uint16_t *ManagedScreen<Job>::auxiliary_frame(unsigned index) const {return session_.auxiliary_frame(index);}

template<class Job>
unsigned ManagedScreen<Job>::frame_slot(const uint16_t *pixels) const {return session_.frame_slot(pixels);}

template<class Job>
bool ManagedScreen<Job>::wait_idle(std::string &error) {return session_.wait_idle(error);}

template<class Job>
FrameSubmission ManagedScreen<Job>::present(const FrameCanvas &canvas,ScreenRegion region,const FramePresentationKey &key,bool incremental,std::string &error,const FrameDamageMap *map) {
  if(paused_.load()){error="screen_paused";return FrameSubmission::Failed;}
  const auto result=session_.present(canvas,region,key,incremental,error,map);
  if(result==FrameSubmission::Failed)log_failure("present",error);
  if(result==FrameSubmission::Unchanged && unchanged_observer_)
    unchanged_observer_(unchanged_context_,canvas);
  return result;
}

template<class Job>
bool ManagedScreen<Job>::recovery_pending() const {return full_redraw_.load()||session_.recovery_pending();}

template<class Job>
void ManagedScreen<Job>::begin_render() {if(full_redraw_.exchange(false))session_.invalidate_frame();session_.begin_render();}

template<class Job>
void ManagedScreen<Job>::consume_recovery() {session_.consume_recovery();}

template<class Job>
void ManagedScreen<Job>::invalidate_frame() {session_.invalidate_frame();}

template<class Job>
void ManagedScreen<Job>::request_redraw() { redraw_.store(true); }

template<class Job>
bool ManagedScreen<Job>::take_redraw() {
  if(paused_.load())return false;
  // Latest-state publication can replace a pending snapshot. The mailbox
  // retains the active snapshot and marks coalescing for a complete repaint.
  if (!LUNAROS_FRESH_FRAME_HANDOFF && ready_ && mailbox_->has_pending()) return false;
  return redraw_.exchange(false);
}

template<class Job>
bool ManagedScreen<Job>::accepts_frame() {
  return ready_ && !paused_.load() && !mailbox_->has_pending();
}

template<class Job>
bool ManagedScreen<Job>::service_render() { if(!ready_)return false;const bool rendered=render_next(); service_timed(); return rendered; }

template<class Job>
void ManagedScreen<Job>::wake() {
  if(!ready_ || stop_requested_.load())return;
  if (dispatch_.request) { dispatch_.request(dispatch_.context); return; }
#ifdef ESP_PLATFORM
  if (caller_execution_) { while (render_next()) {} service_timed(); return; }
  std::lock_guard<std::mutex> admission(producer_mutex_);
  if(!stop_requested_.load())session_.wake_renderer();
#else
  while (render_next()) {}
  service_timed();
#endif
}

template<class Job>
void ManagedScreen<Job>::log_failure([[maybe_unused]] const char *phase,[[maybe_unused]] const std::string &error) {
#ifdef ESP_PLATFORM
  const auto now=clock_();
  if(!last_failure_log_us_ || now-last_failure_log_us_>=1000000){
    last_failure_log_us_=now;
    ESP_LOGW("lunaros.screen","phase=%s error=%s",phase,error.c_str());
  }
#endif
}

template<class Job>
void ManagedScreen<Job>::service_timed() {
  RenderGuard render(render_busy_);
  if (!render || paused_.load() || stop_requested_.load())return;
  if (ready_ && service_.service && service_.service(service_.context)) request_redraw();
}

template<class Job>
bool ManagedScreen<Job>::transfer(ScreenPatchView patch, ScreenRegion region, std::string &error) {
  const auto started=clock_();
  const bool ok=backend_.present_patch(backend_.context,patch,region,error);
  const auto elapsed=clock_()-started;
  if(elapsed>ScreenFrameCadence::period_us)++transfer_budget_misses_;
  session_.metrics_.transferred(ok,elapsed,uint32_t(region.width*region.height*2));
  if (!ok) request_redraw();
  else if (observer_) observer_(observer_context_,patch,region);
  return ok;
}

template<class Job>
bool ManagedScreen<Job>::render_next() {
  lunaredu::system::probe::Scope diagnostic_scope(lunaredu::system::probe::Point::RenderJob);
  RenderGuard render(render_busy_);
  if(!render || !ready_ || stop_requested_.load() || paused_.load())return false;
  auto &queue = *mailbox_;
  uint64_t pending_pacing=0;
  if(LUNAROS_FRESH_FRAME_HANDOFF) {
    if(!queue.has_pending())return false;
    const auto previous_pacing=pacing_wait_us_.load();
    if(!pace_frame())return false;
    pending_pacing=pacing_wait_us_.load()-previous_pacing;
  }
  auto *job = queue.take(); if (!job) return false;
  render_profile_.begin();
  if(render_profile_.active()&&render_profile_.fresh_window()){
    render_profile_baseline_=session_.metrics_.snapshot();render_profile_pacing_baseline_=pacing_wait_us_.load();
  }
  const auto started=clock_(),paced=pacing_wait_us_.load();
  paint_(context_, *job);
  const auto render_elapsed=clock_()-started;
  // Keep the published render timing's original inclusion of frame pacing,
  // even though the wait now happens before taking the immutable snapshot.
  session_.metrics_.elapsed(ScreenPipelinePhase::Render,render_elapsed+pending_pacing);
  if(render_elapsed-(pacing_wait_us_.load()-paced)>ScreenFrameCadence::period_us)++render_budget_misses_;
  queue.finish();
  render_profile_.mark(0);
  const auto measured=render_profile_.finish();
#ifdef ESP_PLATFORM
  if(measured.calls){
    const auto current=session_.metrics_.snapshot();
    [[maybe_unused]] const auto &previous=render_profile_baseline_;
    ESP_LOGI("screen.render","RENDER_PROFILE calls=%u window_us=%llu worker_task_us=%u paint_us=%llu max_paint_us=%u wait_us=%llu compare_us=%llu raster_us=%llu compose_us=%llu animation_us=%llu submitted=%u unchanged=%u completed_patches=%u bytes=%u pacing_us=%llu",
        unsigned(measured.calls),static_cast<unsigned long long>(measured.window_us),unsigned(measured.worker_task_us),
        static_cast<unsigned long long>(measured.wall[0]),unsigned(measured.maximum_us[0]),
        static_cast<unsigned long long>(current.wait_time_us-previous.wait_time_us),static_cast<unsigned long long>(current.compare_time_us-previous.compare_time_us),
        static_cast<unsigned long long>(current.raster_time_us-previous.raster_time_us),static_cast<unsigned long long>(current.compose_time_us-previous.compose_time_us),
        static_cast<unsigned long long>(current.animation_time_us-previous.animation_time_us),unsigned(current.rendered-previous.rendered),unsigned(current.unchanged_frames-previous.unchanged_frames),
        unsigned(current.completed-previous.completed),unsigned(current.transfer_bytes-previous.transfer_bytes),
        static_cast<unsigned long long>(pacing_wait_us_.load()-render_profile_pacing_baseline_));
    render_profile_baseline_=current;
  }
#else
  (void)measured;
#endif
  return true;
}
} // namespace lunaredu::display
