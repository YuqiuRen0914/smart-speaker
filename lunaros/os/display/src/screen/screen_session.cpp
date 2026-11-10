#include "display/internal/screen_session.h"
#include "display/pipeline/geometry.h"
#include <limits>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#ifdef ESP_PLATFORM
#include "kernel/runtime.h"
#include "kernel/task_signal.h"
#include "diagnostics/current_task_runtime.h"
#include "diagnostics/cpu_monitor.h"
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace lunaredu::display {
ScreenMemory external_screen_memory() {
  return {nullptr, [](void *, size_t bytes) -> void * {
#ifdef ESP_PLATFORM
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return std::malloc(bytes);
#endif
  }, [](void *, void *p) {
#ifdef ESP_PLATFORM
    heap_caps_free(p);
#else
    std::free(p);
#endif
  }};
}
}
namespace lunaredu::display::detail {
ScreenSession::ScreenSession(uint64_t (*clock)()) : presenter_(metrics_), clock_(clock) {}
ScreenSession::~ScreenSession() {
  if (retained_ || overlay_bindings_) std::abort();
  std::string error;
  if (!stop(error)) std::abort();
}
void ScreenSession::free_storage() {
  if (destroy_mailbox_ && mailbox_storage_) destroy_mailbox_(mailbox_storage_);
  destroy_mailbox_ = nullptr;
  if (memory_.free) {
    if (mailbox_storage_) memory_.free(memory_.context,mailbox_storage_);
    for (auto *p : frames_) if (p) memory_.free(memory_.context,p);
  }
  mailbox_storage_=nullptr; mailbox_bytes_=0; frames_={}; primary_frames_=0; borrowed_={};
  presenter_.reset(); publish_storage_status();
}
void ScreenSession::release_unstarted() {
  if (retained_ || overlay_bindings_) return;
  std::string error;
  if (!release(error)) {retained_=true;publish_storage_status();return;}
  free_storage(); attempted_=worker_attempted_=worker_failed_=stopped_=false;stopping_.store(false);
  next_=0; backend_={};memory_={};publish_storage_status();
}
void ScreenSession::publish_storage_status() {
  uint32_t count=0;for(auto *p:frames_)if(p)++count;
  const uint64_t bytes=uint64_t(host_.width())*host_.height()*sizeof(uint16_t)*count;
  observed_frames_.store(count);observed_frame_bytes_.store(bytes);observed_mailbox_bytes_.store(mailbox_bytes_);
  observed_retained_bytes_.store(retained_?bytes+mailbox_bytes_:0);
  observed_budget_bytes_.store(budget_.maximum_bytes);observed_reserved_bytes_.store(budget_.reserved_bytes+overlay_bytes_);
  observed_lifecycle_.store(stopped_?ScreenLifecycle::Stopped:ready_?ScreenLifecycle::Running:
      worker_failed_?(retained_?ScreenLifecycle::FailedResident:ScreenLifecycle::Failed):
      count?ScreenLifecycle::Prepared:attempted_?ScreenLifecycle::Failed:ScreenLifecycle::Unconfigured);
}
bool ScreenSession::prepare(ScreenBackend backend,std::string &error,ScreenMemory memory,
    unsigned auxiliary_frames,ScreenStorageBudget budget,size_t mailbox_bytes,unsigned primary_frames) {
  if(attempted_||frames_[0]||stopped_||auxiliary_frames>2||primary_frames<1||primary_frames>2||
      (primary_frames==1&&auxiliary_frames)||!memory.allocate||!memory.free) {
    error="invalid_screen_session";return false;
  }
  size_t required;
  if(!screen_storage_bytes(backend.width,backend.height,primary_frames+auxiliary_frames,mailbox_bytes,budget,required,error))return false;
  if(!host_.configure(backend,error))return false;
  backend_=backend;memory_=memory;budget_=budget;primary_frames_=primary_frames;
  const size_t bytes=size_t(backend.width)*backend.height*sizeof(uint16_t);
  for(unsigned i=0;i<primary_frames+auxiliary_frames;++i) {
    frames_[i]=static_cast<uint16_t*>(memory.allocate(memory.context,bytes));
    if(!frames_[i]){free_storage();error="screen_pipeline_frame_allocation_failed";return false;}
    std::memset(frames_[i],0,bytes);
  }
  if(mailbox_bytes) {
    mailbox_storage_=memory.allocate(memory.context,mailbox_bytes);
    if(!mailbox_storage_){free_storage();error="screen_pipeline_mailbox_allocation_failed";return false;}
    mailbox_bytes_=mailbox_bytes;
  }
  if(clock_ && primary_frames_==2 && !bind_presenter(error)){free_storage();return false;}
  publish_storage_status();error.clear();return true;
}
bool ScreenSession::bind_presenter(std::string &error) {
  FramePresenterPorts ports{this,[](void *p,unsigned slot,std::string &e){return static_cast<ScreenSession*>(p)->wait(slot,e);},
       [](void *p,unsigned slot,ScreenRegion region,std::string &e){return static_cast<ScreenSession*>(p)->submit(slot,&region,1,e);},clock_
#ifdef ESP_PLATFORM
       ,system::cpu_monitor_active,[](){return system::current_task_runtime_us(true);}
#endif
      };
  ports.submit_regions=[](void *p,unsigned slot,const ScreenRegion *regions,size_t count,std::string &e){return static_cast<ScreenSession*>(p)->submit(slot,regions,count,e);};
  ports.window_pixels=(capabilities().window_overhead_bytes+1)/2;
  return presenter_.bind(frames_[0],frames_[1],width(),height(),ports,error);
}
bool ScreenSession::enable_worker(lunaros::KernelRuntime &runtime,const lunaros::ManagedTaskDefinition &task,std::string &error) {
  if(!frames_[0]||worker_attempted_||active()||stopping_||task.lifetime!=lunaros::ManagedTaskLifetime::Resident){error="screen_surface_worker_configuration_invalid";return false;}
  size_t required;
  unsigned frame_count=0;for(auto *p:frames_)if(p)++frame_count;
  if(!frames_[1])++frame_count;
  if(!screen_storage_bytes(width(),height(),frame_count,mailbox_bytes_+overlay_bytes_,budget_,required,error))return false;
  worker_attempted_=true;
  const bool added=!frames_[1];
  if(added){const size_t bytes=size_t(width())*height()*sizeof(uint16_t);
    frames_[1]=static_cast<uint16_t*>(memory_.allocate(memory_.context,bytes));
    if(!frames_[1]){error="display_second_frame_unavailable";return false;}
    std::memset(frames_[1],0,bytes);
  }
  if(!display_.start(runtime,host_,task,frames_[0],frames_[1],width(),height(),capabilities().native_format,error)) {
    retained_=display_.ready();worker_failed_=retained_;
    if(added&&!retained_){memory_.free(memory_.context,frames_[1]);frames_[1]=nullptr;}
    publish_storage_status();return false;
  }
  primary_frames_=2;retained_=true;
  backend_.capabilities.asynchronous_transfer=true;
  if(!host_.configure(backend_,error)){worker_failed_=true;publish_storage_status();return false;}
  publish_storage_status();error.clear();return true;
}
bool ScreenSession::start(lunaros::KernelRuntime &runtime,const char *owner,ScreenPipelineTasks tasks,
    lunaros::ManagedTaskFunction render,void *context,RendererRetirement retirement,std::string &error) {
#ifdef ESP_PLATFORM
  if(attempted_||!frames_[0]||!render||!retirement.request_stop||!retirement.finished||
      !tasks.renderer.id||std::strlen(tasks.renderer.id)>=renderer_id_.size()||
      tasks.renderer.lifetime!=lunaros::ManagedTaskLifetime::Resident){error="invalid_screen_pipeline_start";return false;}
  attempted_=true;
  if(!enable_worker(runtime,tasks.display,error)||!acquire(owner,error)){worker_failed_=true;if(!retained_)free_storage();publish_storage_status();return false;}
  runtime_=&runtime;retirement_=retirement;
  std::memcpy(renderer_id_.data(),tasks.renderer.id,std::strlen(tasks.renderer.id)+1);
  if(!lunaros::create_managed_task(runtime,tasks.renderer,render,context,&renderer_,error)){ready_=false;worker_failed_=true;publish_storage_status();return false;}
  ready_=true;publish_storage_status();error.clear();return true;
#else
  (void)runtime;(void)owner;(void)tasks;(void)render;(void)context;(void)retirement;
  error="screen_pipeline_workers_require_esp";return false;
#endif
}
bool ScreenSession::start_direct(const char *owner,std::string &error) {
  if(attempted_||!frames_[0]){error="invalid_screen_pipeline_start";return false;}
  attempted_=true;
  if(!acquire(owner,error)){free_storage();return false;}
  return true;
}
bool ScreenSession::stop(std::string &error) {
  stopping_=true;
#ifdef ESP_PLATFORM
  if(renderer_) {
    retirement_.request_stop(retirement_.context);
    wake_renderer();
    unsigned waits=0;
    while(!retirement_.finished(retirement_.context)||runtime_->task_active(renderer_id_.data())) {
      if(++waits>500){error="screen_renderer_stop_timeout";return false;}
      vTaskDelay(std::max<TickType_t>(1,pdMS_TO_TICKS(1)));
    }
    renderer_=nullptr;runtime_=nullptr;retirement_={};observed_renderer_stack_.store(0);
  }
#endif
  if(display_.ready()) {if(!display_.stop(error))return false;}
  else if(!release(error))return false;
  retained_=ready_=worker_failed_=false;stopped_=true;borrowed_={};free_storage();error.clear();return true;
}
bool ScreenSession::acquire(const char *owner,std::string &error) {
  if(!frames_[0]||stopping_||active()||worker_failed_||ownership_generation_==UINT64_MAX){error="screen_surface_owner_invalid";return false;}
  if(!(asynchronous()?display_.acquire(owner,error):host_.acquire(owner,lease_,error)))return false;
  if(!asynchronous())direct_active_.store(true);
  ++ownership_generation_;next_=0;borrowed_={};ready_=true;publish_storage_status();return true;
}
bool ScreenSession::release(std::string &error) {
  if(asynchronous()){if(!display_.release(error))return false;}
  else if(lease_.generation&&!host_.release(lease_,error))return false;
  lease_={};direct_active_.store(false);borrowed_={};ready_=false;publish_storage_status();error.clear();return true;
}
bool ScreenSession::wait(unsigned slot,std::string &error) {
  return asynchronous()?display_.wait(slot,error):host_.wait_idle(lease_,error);
}
bool ScreenSession::wait_all(std::string &error) {
  if(asynchronous())return display_.wait_all(error);
  if(active())return host_.wait_idle(lease_,error);
  error.clear();return true;
}
bool ScreenSession::wait_idle(std::string &error) {
  return asynchronous()?display_.drain(error):host_.wait_idle(lease_,error);
}
bool ScreenSession::bind_authority(ScreenAuthority authority,const char *output,std::string &error){return host_.bind_authority(authority,output,error);}
FrameCanvas ScreenSession::borrow(FrameCanvas canvas,unsigned slot,std::string &error) {
  if(!canvas.pixels)return {};
  if(!active()||stopping_||slot>=2||generation_==UINT64_MAX){error="screen_frame_lease_unavailable";return {};}
  canvas.issuer=this;canvas.slot=slot;canvas.generation=++generation_;borrowed_[slot]=canvas.generation;canvases_[slot]=canvas;error.clear();return canvas;
}
FrameCanvas ScreenSession::begin_frame(std::string &error) {
  if(!active()||stopping_){error="display_lease_inactive";return {};}
  if(!wait(next_,error))return {};
  return borrow({frames_[next_],width(),height(),width()},next_,error);
}
FrameCanvas ScreenSession::begin_frame(uint16_t w,uint16_t h,uint16_t color,uint16_t y,bool clear,std::string &error) {
  if(!active()||stopping_){error="display_lease_inactive";return {};}
  auto canvas=presenter_.begin(w,h,color,y,clear,error);
  return borrow(canvas,presenter_.slot(),error);
}
bool ScreenSession::validate(const FrameCanvas &canvas,std::string &error) const {
  if(!holds_frame(canvas)) {
    error="screen_frame_lease_invalid";return false;
  }
  return true;
}
bool ScreenSession::holds_frame(const FrameCanvas &canvas) const {
  return active()&&!stopping_.load()&&canvas.issuer==this&&canvas.slot<2&&canvas.generation&&
      borrowed_[canvas.slot]==canvas.generation&&canvas.pixels==canvases_[canvas.slot].pixels&&
      canvas.stride==canvases_[canvas.slot].stride&&canvas.width==canvases_[canvas.slot].width&&
      canvas.height==canvases_[canvas.slot].height;
}
void ScreenSession::retain_overlay(){++overlay_bindings_;}
void ScreenSession::release_overlay_binding(){if(!overlay_bindings_)std::abort();--overlay_bindings_;}
void ScreenSession::consume(const FrameCanvas &canvas){borrowed_[canvas.slot]=0;}
bool ScreenSession::submit(unsigned slot,const ScreenRegion *regions,size_t count,std::string &error) {
  if(!regions||slot>=primary_frames_||count>kScreenBatchMaxRegions){error="display_batch_invalid";return false;}
  std::array<ScreenRegion,kScreenBatchMaxRegions> aligned{};
  for(size_t i=0;i<count;++i)if(!align_screen_region(regions[i],width(),height(),capabilities(),aligned[i],error))return false;
  if(!count){error.clear();return true;}
  if(asynchronous())return display_.present(slot,aligned.data(),count,error,canvases_[slot]);
  for(size_t i=0;i<count;++i)if(!host_.present(lease_,{frames_[slot],width(),height(),width(),capabilities().native_format},aligned[i],error))return false;
  if(!host_.wait_idle(lease_,error))return false;
  host_.frame_presented(lease_,{frames_[slot],width(),height(),width(),capabilities().native_format,
      this,canvases_[slot].generation,slot},aligned.data(),count);
  error.clear();return true;
}
bool ScreenSession::present(const FrameCanvas &canvas,const ScreenRegion *regions,size_t count,std::string &error) {
  if(!validate(canvas,error)||!submit(canvas.slot,regions,count,error))return false;
  consume(canvas);next_=asynchronous()?(canvas.slot+1)%2:0;return true;
}
FrameSubmission ScreenSession::present(const FrameCanvas &canvas,ScreenRegion region,const FramePresentationKey &key,
    bool incremental,std::string &error,const FrameDamageMap *map) {
  if(!validate(canvas,error)||canvas.slot!=presenter_.slot()){if(error.empty())error="screen_frame_lease_invalid";return FrameSubmission::Failed;}
  const auto result=presenter_.present(region,key,incremental,error,map);
  if(result!=FrameSubmission::Failed)consume(canvas);
  return result;
}
bool ScreenSession::reserve_overlay(size_t bytes,std::string &error) {
  if(bytes>std::numeric_limits<size_t>::max()-overlay_bytes_||mailbox_bytes_>std::numeric_limits<size_t>::max()-overlay_bytes_-bytes){error="screen_storage_budget_exceeded";return false;}
  size_t required;unsigned count=0;for(auto *p:frames_)if(p)++count;
  if(!screen_storage_bytes(width(),height(),count,mailbox_bytes_+overlay_bytes_+bytes,budget_,required,error))return false;
  overlay_bytes_+=bytes;publish_storage_status();return true;
}
void ScreenSession::release_overlay(size_t bytes){if(bytes>overlay_bytes_)std::abort();overlay_bytes_-=bytes;publish_storage_status();}
bool ScreenSession::ready() const{return frames_[0]&&!worker_failed_&&!stopping_;}
bool ScreenSession::active() const{return asynchronous()?display_.active():direct_active_.load();}
bool ScreenSession::retained() const{return retained_;}
bool ScreenSession::asynchronous() const{return display_.ready();}
bool ScreenSession::worker_failed() const{return worker_failed_;}
ScreenLifecycle ScreenSession::lifecycle() const{return observed_lifecycle_.load();}
ScreenStorageSnapshot ScreenSession::storage_snapshot() const{return {lifecycle(),observed_frames_.load(),observed_frame_bytes_.load(),observed_mailbox_bytes_.load(),observed_retained_bytes_.load(),observed_budget_bytes_.load(),observed_reserved_bytes_.load()};}
ScreenSessionSnapshot ScreenSession::snapshot() const {
  ScreenSessionSnapshot value;static_cast<ScreenPipelineSnapshot&>(value)=metrics_.snapshot();value.storage=storage_snapshot();value.control=display_.snapshot();
  value.renderer_stack_free=observed_renderer_stack_.load();
  return value;
}
uint16_t ScreenSession::width() const{return frames_[0]?host_.width():0;}
uint16_t ScreenSession::height() const{return frames_[0]?host_.height():0;}
ScreenCapabilities ScreenSession::capabilities() const{return host_.capabilities();}
uint16_t *ScreenSession::frame(unsigned index) const{return index<frames_.size()?frames_[index]:nullptr;}
uint16_t *ScreenSession::active_frame() const{return presenter_.frame();}
unsigned ScreenSession::active_slot() const{return presenter_.slot();}
uint16_t *ScreenSession::auxiliary_frame(unsigned index) const{return index<2?frame(index+2):nullptr;}
unsigned ScreenSession::frame_slot(const uint16_t *pixels) const {
  const auto address=reinterpret_cast<uintptr_t>(pixels);
  for(unsigned i=0;i<2;++i){const auto begin=reinterpret_cast<uintptr_t>(frames_[i]);if(begin&&address>=begin&&address<begin+size_t(width())*height()*sizeof(uint16_t))return i;}
  return 2;
}
bool ScreenSession::recovery_pending() const{return presenter_.recovery_pending();}
void ScreenSession::begin_render(){presenter_.begin_render();}
void ScreenSession::consume_recovery(){presenter_.consume_recovery();}
void ScreenSession::invalidate_frame(){presenter_.invalidate();}
void ScreenSession::wake_renderer() {
#ifdef ESP_PLATFORM
  if(renderer_&&!retirement_.finished(retirement_.context))lunaros::task_signal::notify(renderer_);
#endif
}
void ScreenSession::sample_renderer_stack() {
#ifdef ESP_PLATFORM
  observed_renderer_stack_.store(uint32_t(uxTaskGetStackHighWaterMark(nullptr)));
#endif
}

}
