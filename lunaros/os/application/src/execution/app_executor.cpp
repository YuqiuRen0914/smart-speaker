#include "kernel/task_signal.h"
#include "app/execution/backend_catalog.h"
#include "app/execution/app_executor.h"
#include "app/package/application_profile.h"
#include "session_diagnostics.h"
#include "kernel/runtime.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <esp_timer.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
namespace lunaredu::lunaros::apps {
namespace { constexpr const char *kTaskId = "os.app.foreground"; }
struct AppExecutor::Impl {
  enum class Request { None,Open,Save,Validate };
  struct Event {int kind,a,b,c;};
  mutable std::mutex mutex;
  SessionDiagnostics diagnostics; // Outlives the backend's final callbacks.
  std::unique_ptr<ApplicationSession> session;
  KernelRuntime *runtime = nullptr;
  ManagedTaskHandle task = nullptr;
  SemaphoreHandle_t done = nullptr;
  Request pending = Request::None;
  std::shared_ptr<ApplicationOperationSource> checkpoint_completion;
  bool processing = false;
  ApplicationEntry entry;
  ApplicationEntryPreparation prepare = nullptr;
  void *preparation_context = nullptr;
  app::ApplicationStartResult opening = app::ApplicationStartResult::Failed;
  std::string backend, error;
  SessionStorage storage{}, candidate{};
  uint32_t seed = 0;
  int hz = 0;
  bool active = false, result = false, closing = false, quit = false, exited = true;
  bool final_save_attempted=false;
  uint32_t close_attempt = 0, close_completed = 0;
  bool close_result = false;
  std::string close_error;
  std::shared_ptr<const DrawList> frame;
  std::array<Event,kApplicationBudget.input_events> events{};
  size_t count = 0;
  std::atomic<bool> cancelled{false};

  ~Impl() { if (done) vSemaphoreDelete(done); }
  bool request(Request value) {
    {
      std::lock_guard lock(mutex);
      if (!task || closing) { error = "application_executor_closing"; return false; }
      if (pending != Request::None || processing || checkpoint_completion || opening == app::ApplicationStartResult::Pending) {
        error = "application_executor_busy"; return false;
      }
      (void)xSemaphoreTake(done, 0);
      pending = value;
      if (value == Request::Open) count = 0;
      lunaredu::lunaros::task_signal::notify(task);
    }
    // Operations borrow storage callbacks: acknowledge before their caller
    // releases them. Stop has a separate bounded handshake with retained state.
    xSemaphoreTake(done,portMAX_DELAY);
    std::lock_guard lock(mutex);
    return result;
  }
  static void task_entry(void *context) { static_cast<Impl *>(context)->run(); }
  void run() {
    int64_t last_tick=esp_timer_get_time(),last_report=last_tick,last_save=last_tick;
    uint32_t ticks=0,frames=0;uint64_t compute_us=0;
    for (;;) {
      Request command; bool stopping; uint32_t attempt;
      std::shared_ptr<ApplicationOperationSource> completion;
      { std::lock_guard lock(mutex); command=pending;pending=Request::None;processing=command!=Request::None;stopping=quit;attempt=close_attempt;
        if(command==Request::Save)completion=checkpoint_completion; }
      if (stopping) {
        std::string detail;
        if (!final_save_attempted&&session&&session->active()&&storage.write) {
          // Allow a final save on the owner. Backend callback budgets still
          // bound runaway guest code; new input remains blocked throughout.
          final_save_attempted=true;cancelled=false;
          (void)session->checkpoint(detail);
          cancelled=true;
        }
        const bool ok=!session || session->close(detail);
        if (ok) session.reset();
        {
          std::lock_guard lock(mutex);
          active=session&&session->active();frame=session?session->frame():nullptr;
          close_completed=attempt;close_result=ok;close_error=detail;quit=false;
          if (!ok) error=detail;
          else { task=nullptr;exited=true; }
          if (command!=Request::None) result=false;
          processing=false;
          opening=app::ApplicationStartResult::Failed;
          if(completion&&checkpoint_completion==completion)checkpoint_completion.reset();
        }
        if(completion)completion->complete(ApplicationOperationPhase::Cancelled,
            detail.empty()?"application_executor_closing":detail);
        else if (command==Request::Save||command==Request::Validate) xSemaphoreGive(done);
        if (ok) return;
      } else if (command!=Request::None) {
        std::string detail; bool ok=true;
        if (command==Request::Save) ok=!session||session->checkpoint(detail);
        else if (command==Request::Validate) ok=session&&session->validate_save(candidate,detail);
        else {
          if (prepare) ok=prepare(preparation_context,entry,detail);
          if (cancelled) { ok=false;detail="application_start_cancelled"; }
          if (ok && (entry.offset >= entry.bytes.size() || entry.backend.empty())) {
            ok=false;detail="invalid application entry";
          }
          if (ok && session) { ok=session->close(detail);if(ok)session.reset(); }
          if (ok) {
            final_save_attempted=false;
            backend=entry.backend;hz=entry.tick_hz;seed=entry.seed;storage=entry.storage;
            if(!storage.observe){storage.observe=SessionDiagnostics::observe;storage.observation_context=&diagnostics;}
            storage.cancel_context=this;storage.cancelled=[](void *p){return static_cast<Impl *>(p)->cancelled.load();};
            session=create_application_session(backend,*runtime);
            ok=session&&session->open(entry.bytes.data()+entry.offset,entry.bytes.size()-entry.offset,storage,seed,detail);
            if(!session&&detail.empty())detail="应用执行后端不可用";
          }
          // The session consumes entry bytes during open. The owned payload
          // remains intact if cancellation/timeout occurs before open returns.
          if(ok&&!cancelled)std::vector<uint8_t>().swap(entry.bytes);
        }
        bool checkpoint_cancelled=false;
        { std::lock_guard lock(mutex);active=session&&session->active();frame=session?session->frame():nullptr;error=detail;result=ok;processing=false;
          if(command==Request::Open)opening=ok&&!closing?app::ApplicationStartResult::Started:app::ApplicationStartResult::Failed;
          if(completion){checkpoint_cancelled=closing;if(checkpoint_completion==completion)checkpoint_completion.reset();} }
        last_tick=last_report=last_save=esp_timer_get_time();ticks=frames=0;compute_us=0;
        // Open has a polled result, not a semaphore waiter. Its notification
        // must never acknowledge a subsequently queued checkpoint/validation.
        if(completion)completion->complete(checkpoint_cancelled?ApplicationOperationPhase::Cancelled:
            ok?ApplicationOperationPhase::Succeeded:ApplicationOperationPhase::Failed,
            checkpoint_cancelled&&detail.empty()?"application_executor_closing":detail);
        else if(command!=Request::Open)xSemaphoreGive(done);
        continue;
      }
      std::array<Event,kApplicationBudget.input_events> batch;size_t n;bool running;int rate;
      { std::lock_guard lock(mutex);n=count;std::copy_n(events.begin(),n,batch.begin());count=0;running=active&&!closing;rate=hz; }
      std::string detail;const auto began=esp_timer_get_time();
      if(running){
        for(size_t i=0;i<n&&session->active()&&!cancelled;++i){const auto &e=batch[i];if(!session->event(e.kind,e.a,e.b,e.c,detail))break;}
        const auto now=esp_timer_get_time();
        if(detail.empty()&&rate&&session->active()&&!cancelled&&now-last_tick>=1000000/rate){
          const int elapsed=std::min<int64_t>(100,(now-last_tick)/1000);last_tick=now;
          (void)session->event(7,elapsed,0,0,detail);++ticks;
        }
        if(detail.empty()&&session->active()&&!cancelled&&now-last_save>=5000000){last_save=now;(void)session->checkpoint(detail);}
        compute_us+=esp_timer_get_time()-began;
        {std::lock_guard lock(mutex);if(frame!=session->frame())++frames;active=session->active();frame=session->frame();if(!detail.empty())error=detail;}
        if(esp_timer_get_time()-last_report>=5000000){
          ESP_LOGI("lunaros_app","worker ticks=%u frames=%u compute_us=%u window_us=%u",ticks,frames,unsigned(compute_us),unsigned(esp_timer_get_time()-last_report));
          last_report=esp_timer_get_time();ticks=frames=0;compute_us=0;
        }
      }
      const int64_t until=running&&rate?std::max<int64_t>(1000,1000000/rate-(esp_timer_get_time()-last_tick)):20000;
      lunaredu::lunaros::task_signal::wait_ms(std::max<uint32_t>(1,unsigned(until/1000)));
    }
  }
};
AppExecutor::AppExecutor():impl_(new Impl){}
AppExecutor::~AppExecutor(){
  std::string error;
  if(!close(error)){
    // The worker still owns this state. Retain it instead of destroying a live
    // mutex/session or forcibly deleting a native task.
    ESP_LOGE("lunaros_app","retaining executor after stop failure: %s",error.c_str());
    (void)impl_.release();
  }
}
bool AppExecutor::start(KernelRuntime &runtime,std::string &error,const char *owner_service_id){
  auto &s=*impl_;std::lock_guard lock(s.mutex);error.clear();
  if(s.closing){error="application_executor_closing";return false;}
  if(s.task){if(s.runtime==&runtime)return true;error="application_executor_owner_mismatch";return false;}
  if (!owner_service_id && !runtime.declare_task_owner(ManagedTaskOwnerKind::Subsystem, "core.apps", error)) return false;
  s.runtime=&runtime;
  if(!s.done)s.done=xSemaphoreCreateBinary();
  if(!s.done){s.runtime=nullptr;error="app completion allocation failed";return false;}
  s.exited=false;
  ManagedTaskDefinition task{.id=kTaskId,.owner_service_id=owner_service_id ? owner_service_id : "core.apps",.role="application",
      .stack_bytes=kApplicationBudget.executor_stack_bytes,.priority=1,.core_affinity=1,.lifetime=ManagedTaskLifetime::Feature};
  task.owner_kind = owner_service_id ? ManagedTaskOwnerKind::Service : ManagedTaskOwnerKind::Subsystem;
  if(!create_managed_task(runtime,task,Impl::task_entry,&s,&s.task,error)){
    // No task was acquired. Do not later wait on another executor which owns
    // the same task ID in this Kernel, or retain a semaphore after rejection.
    s.exited=true;s.runtime=nullptr;s.task=nullptr;
    vSemaphoreDelete(s.done);s.done=nullptr;return false;
  }
  return true;
}
bool AppExecutor::open(ApplicationEntry entry,std::string &error){
  if(entry.offset>=entry.bytes.size()||entry.backend.empty()){error="invalid application entry";return false;}
  auto &s=*impl_;
  {
    std::lock_guard lock(s.mutex);
    if(!s.task||s.closing){error="application_executor_closing";return false;}
    if(s.pending!=Impl::Request::None||s.processing||s.opening==app::ApplicationStartResult::Pending){error="application_executor_busy";return false;}
    s.entry=std::move(entry);
    s.prepare=nullptr;s.preparation_context=nullptr;s.cancelled=false;s.error.clear();s.count=0;
    s.opening=app::ApplicationStartResult::Pending;s.pending=Impl::Request::Open;
    lunaredu::lunaros::task_signal::notify(s.task);
  }
  // Synchronous command/import callers retain a bounded wrapper. Foreground
  // callers use begin_open/open_result and never wait on this task.
  const auto deadline=esp_timer_get_time()+30'000'000;
  for(;;){
    const auto result=open_result(error);
    if(result==app::ApplicationStartResult::Started)return true;
    if(result==app::ApplicationStartResult::Failed)break;
    if(esp_timer_get_time()>=deadline){error="application_start_timeout";break;}
    vTaskDelay(1);
  }
  std::string cleanup;if(!close(cleanup,0))error+="; "+cleanup;
  return false;
}
bool AppExecutor::begin_open(ApplicationEntryPreparation prepare,void *context,std::string &error){
  auto &s=*impl_;std::lock_guard lock(s.mutex);error.clear();
  if(!s.task||s.closing){error="application_executor_closing";return false;}
  if(!prepare||!context){error="application_preparation_missing";return false;}
  if(s.pending!=Impl::Request::None||s.processing||s.active||s.opening==app::ApplicationStartResult::Pending){error="application_executor_busy";return false;}
  s.entry={};s.prepare=prepare;s.preparation_context=context;s.cancelled=false;s.error.clear();s.count=0;
  s.opening=app::ApplicationStartResult::Pending;s.pending=Impl::Request::Open;
  lunaredu::lunaros::task_signal::notify(s.task);return true;
}
app::ApplicationStartResult AppExecutor::open_result(std::string &error)const{
  auto &s=*impl_;std::lock_guard lock(s.mutex);
  error=s.opening==app::ApplicationStartResult::Failed?s.error:std::string{};
  return s.opening;
}
int AppExecutor::tick_hz()const{std::lock_guard lock(impl_->mutex);return impl_->opening==app::ApplicationStartResult::Started?impl_->entry.tick_hz:0;}
bool AppExecutor::checkpoint(std::string &error){bool ok=impl_->request(Impl::Request::Save);error=failure();return ok;}
bool AppExecutor::begin_checkpoint(ApplicationOperation &receipt,std::string &error){
  receipt={};error.clear();auto &s=*impl_;std::lock_guard lock(s.mutex);
  if(!s.task||s.closing){error="application_executor_closing";return false;}
  if(s.pending!=Impl::Request::None||s.processing||s.checkpoint_completion||s.opening==app::ApplicationStartResult::Pending){
    error="application_executor_busy";return false;
  }
  s.checkpoint_completion=std::make_shared<ApplicationOperationSource>();
  receipt=s.checkpoint_completion->receipt();s.pending=Impl::Request::Save;
  lunaredu::lunaros::task_signal::notify(s.task);return true;
}
bool AppExecutor::validate_save(SessionStorage storage,std::string &error){
  {std::lock_guard lock(impl_->mutex);impl_->candidate=storage;}
  bool ok=impl_->request(Impl::Request::Validate);error=failure();return ok;
}
bool AppExecutor::close(std::string &error,uint32_t timeout_ms){
  auto &s=*impl_;error.clear();uint32_t attempt;
  {
    std::lock_guard lock(s.mutex);
    if(!s.runtime)return true;
    s.closing=true;s.cancelled=true;s.count=0;
    if(s.task&&!s.quit){s.quit=true;++s.close_attempt;lunaredu::lunaros::task_signal::notify(s.task);}
    attempt=s.close_attempt;
  }
  const int64_t deadline=esp_timer_get_time()+int64_t(timeout_ms)*1000;
  for(;;){
    bool exited;
    {std::lock_guard lock(s.mutex);exited=s.exited;if(!exited&&s.close_completed==attempt&&!s.close_result){error=s.close_error.empty()?"application_stop_failed":s.close_error;return false;}}
    if(exited&&!s.runtime->task_active(kTaskId)){
      std::lock_guard lock(s.mutex);
      s.entry={};s.prepare=nullptr;s.preparation_context=nullptr;s.opening=app::ApplicationStartResult::Failed;s.processing=false;
      s.storage={};s.candidate={};s.backend.clear();s.frame.reset();s.active=false;s.closing=false;s.cancelled=false;s.runtime=nullptr;
      s.checkpoint_completion.reset();
      if(s.done){vSemaphoreDelete(s.done);s.done=nullptr;}
      return true;
    }
    if(esp_timer_get_time()>=deadline){error="application_stop_timeout";return false;}
    vTaskDelay(1);
  }
}
bool AppExecutor::active()const{std::lock_guard lock(impl_->mutex);return impl_->active;}
std::shared_ptr<const DrawList> AppExecutor::frame()const{std::lock_guard lock(impl_->mutex);return impl_->frame;}
std::string AppExecutor::failure()const{std::lock_guard lock(impl_->mutex);return impl_->error;}
bool AppExecutor::input(int kind,int a,int b,int c){
  auto &s=*impl_;std::lock_guard lock(s.mutex);if(!s.active||s.closing||!s.task)return false;
  if(kind==8){for(size_t i=s.count;i>0;--i){auto &e=s.events[i-1];if(e.kind!=8)break;if(e.a==a){e={kind,a,b,c};return true;}}}
  else if(kind==9&&s.count&&s.events[s.count-1].kind==9){const auto &last=s.events[s.count-1];if(last.a==a&&last.b==b&&last.c==c)return true;}
  else if(kind==3&&s.count&&s.events[s.count-1].kind==3){s.events[s.count-1]={kind,a,b,c};return true;}
  if(s.count==s.events.size()){s.count=1;s.events[0]={6,0,0,0};return false;}
  s.events[s.count++]={kind,a,b,c};lunaredu::lunaros::task_signal::notify(s.task);return true;
}
}
