#include "realtime/realtime_pcm_backend.h"
#include "kernel/runtime.h"
#include "kernel/managed_task.h"
#include <freertos/task.h>
#include <freertos/queue.h>
#include "host_event.h"
#include <atomic>
#include <cassert>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <vector>
#include <memory>
#include <source_location>
#ifdef _WIN32
#undef assert
#define assert(value) ((value)?void(0):(std::cerr<<"Assertion failed: "<<#value<<" line "<<__LINE__<<std::endl,std::exit(1)))
#endif
namespace lunaredu::system { bool worker_work_profiling_active(){return false;} }
using namespace lunaredu;
#ifndef CONFIG_LUNAROS_AUDIO_REALTIME_OUTPUT_CORE
#define CONFIG_LUNAROS_AUDIO_REALTIME_OUTPUT_CORE 0
#endif
struct AudioTestQueue {size_t depth,size;std::mutex mutex;std::deque<std::vector<uint8_t>> data;};
struct tskTaskControlBlock {
  std::unique_ptr<host_test::Event> signal;
  std::unique_ptr<host_test::Thread> thread;
  std::atomic<unsigned> notifications{0},waits{0},timed_waits{0};
  std::atomic<bool> active{false},returned{false},hold_wrapper{false};
  lunaros::ManagedTaskFunction function=nullptr;void *context=nullptr;
} task;
namespace {
std::atomic<int> queues{0};
bool creation_allowed=true;
std::atomic<int64_t> clock_offset_us{0};
template<class Predicate> void await(Predicate predicate,std::source_location at=std::source_location::current()){
  const auto deadline=host_test::now_ms()+3000;
  while(!predicate()){
    if(host_test::now_ms()>=deadline){std::cerr<<"PCM wait timed out at line "<<at.line()<<'\n';std::abort();}
    host_test::pause_ms(1);
  }
}
void settle_idle(){host_test::pause_ms(10);assert(task.active);}
void join(){await([]{return task.returned&&!task.active;});task.thread->join();task.thread.reset();task.signal.reset();}
struct Hardware final:audio::PcmBackend {
  std::atomic<unsigned> writes{0},idle_releases{0};
  std::atomic<bool> enabled{false},input{false},fail_write{false},fail_release{false};
  std::atomic<bool> hold_write{false}, in_write{false}, fail_disable{false};
  int input_sample_rate()const override{return 16000;}
  int output_sample_rate()const override{return 16000;}
  bool enable_input(bool value)override{input=value;return true;}
  bool enable_output(bool value)override{if(!value && fail_disable)return false;enabled=value;return true;}
  bool configure_output_sample_rate(uint32_t)override{return true;}
  bool read_input(int16_t *,size_t,uint32_t)override{return true;}
  bool write_output(const int16_t *,size_t,uint16_t,uint32_t)override{in_write=true;++writes;while(hold_write)host_test::pause_ms(1);in_write=false;return !fail_write;}
  bool release_idle_resources()override{assert(!input && !enabled);if(fail_release)return false;++idle_releases;return true;}
};
}
int64_t esp_timer_get_time(){return int64_t(host_test::now_ms())*1000+clock_offset_us.load();}
void vTaskDelay(TickType_t ticks){host_test::pause_ms(ticks);}
void xTaskNotifyGiveIndexed(TaskHandle_t target,UBaseType_t index){
  assert(target==&task && index==1 && task.active && !task.returned);
  ++task.notifications;task.signal->set();
}
uint32_t ulTaskNotifyTakeIndexed(UBaseType_t index,BaseType_t clear,TickType_t ticks){
  assert(index==1 && clear==pdTRUE);++task.waits;if(ticks!=portMAX_DELAY)++task.timed_waits;
  auto value=task.notifications.exchange(0);if(value)return value;
  (void)task.signal->wait(ticks);
  return task.notifications.exchange(0);
}
QueueHandle_t xQueueCreate(size_t depth,size_t size){++queues;return new AudioTestQueue{depth,size,{},{}};}
void vQueueDelete(QueueHandle_t q){assert(!task.active);delete q;--queues;}
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q){
  std::lock_guard<std::mutex> lock(q->mutex);return UBaseType_t(q->data.size());
}
BaseType_t xQueueSend(QueueHandle_t q,const void *data,TickType_t){
  std::lock_guard<std::mutex> lock(q->mutex);if(q->data.size()==q->depth)return pdFALSE;
  auto p=static_cast<const uint8_t *>(data);q->data.emplace_back(p,p+q->size);return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q,void *data,TickType_t ticks){
  const auto deadline=host_test::now_ms()+ticks;
  for(;;){
    {std::lock_guard<std::mutex> lock(q->mutex);if(!q->data.empty()){
      std::memcpy(data,q->data.front().data(),q->size);q->data.pop_front();return pdTRUE;
    }}
    if(!ticks || host_test::now_ms()>=deadline)return pdFALSE;
    host_test::pause_ms(1);
  }
}
namespace lunaredu::lunaros {
KernelRuntime::KernelRuntime()=default;
KernelRuntime::~KernelRuntime()=default;
bool KernelRuntime::task_active(const char *id)const{
  assert(std::strcmp(id,"audio.realtime_pcm")==0);return task.active;
}
bool KernelRuntime::task_active(const char *id,ManagedTaskOwnerKind,const char *)const{
  assert(std::strcmp(id,"audio.realtime_pcm")==0);return task.active;
}
bool create_managed_task(KernelRuntime &,const ManagedTaskDefinition &definition,ManagedTaskFunction function,
                        void *context,ManagedTaskHandle *handle,std::string &error){
  assert(definition.stack_bytes==8192 && definition.priority==18 &&
         definition.core_affinity==CONFIG_LUNAROS_AUDIO_REALTIME_OUTPUT_CORE);
  if(!creation_allowed){error="injected_create_failure";return false;}
  assert(!task.active && !task.thread);task.notifications=task.waits=task.timed_waits=0;
  task.active=true;task.returned=false;task.function=function;task.context=context;
  task.signal=std::make_unique<host_test::Event>();*handle=&task;
  task.thread=std::make_unique<host_test::Thread>([]{
    task.function(task.context);task.returned=true;
    while(task.hold_wrapper)host_test::pause_ms(1);
    task.active=false;
  });return true;
}
}
int main(){
  lunaros::KernelRuntime runtime;std::string error;
  std::array<int16_t,128> samples{};samples.fill(1000);
  creation_allowed=false;
  auto failed=audio::create_realtime_pcm_backend(std::make_unique<Hardware>(),runtime,"audio",error);
  assert(failed && queues==0 && !task.active);
  assert(!failed->enable_output(true) && queues==0 && !task.active);
  failed.reset();creation_allowed=true;
  for(int cycle=0;cycle<20;++cycle){
    auto hardware=std::make_unique<Hardware>();auto *observed=hardware.get();
    auto backend=audio::create_realtime_pcm_backend(std::move(hardware),runtime,"audio",error);assert(backend);
    assert(!task.active && queues==0 && observed->writes==0);
    assert(backend->enable_input(true)); // Wake listening has no output worker.
    assert(!task.active&&queues==0);
    assert(backend->prepare_output()); // Real turn prepares before server PCM.
    await([]{return task.waits>0;});host_test::pause_ms(10);
    const auto idle_waits=task.waits.load();host_test::pause_ms(30);
    assert(task.waits==idle_waits && observed->writes==0&&!observed->enabled);
    assert(backend->enable_output(true));
    assert(backend->write_output(samples.data(),samples.size(),32767,12));
    await([&]{return backend->diagnostics().completed_frames==128;});
    // Pause retains all 16 PCM blocks. Full capacity is preserved, then resume
    // wakes immediately; no periodic retry is needed to process the first block.
    assert(backend->pause_output(true));settle_idle();const auto paused_writes=observed->writes.load();
    for(int i=0;i<16;++i)assert(backend->write_output(samples.data(),samples.size(),32767,12));
    assert(!backend->write_output(samples.data(),samples.size(),32767,1));host_test::pause_ms(15);
    assert(observed->writes==paused_writes && backend->diagnostics().pending_frames==2048);
    assert(backend->pause_output(false));await([&]{return backend->diagnostics().completed_frames==2176;});
    assert(backend->enable_output(false));await([&]{return !observed->enabled.load();});
    assert(backend->diagnostics().output_muted && backend->diagnostics().pending_frames==0);
    const auto muted_waits=task.waits.load();host_test::pause_ms(30);assert(task.waits==muted_waits);
    // A shutdown wake escapes even a paused infinite wait, while the managed
    // wrapper still retains queues and refuses a premature retirement.
    assert(backend->enable_input(false));
    assert(backend->pause_output(true));settle_idle();task.hold_wrapper=true;
    assert(!backend->shutdown(error));await([]{return task.returned.load();});
    assert(!backend->shutdown(error) && queues==2);
    task.hold_wrapper=false;await([]{return !task.active.load();});
    assert(backend->shutdown(error) && queues==0);backend.reset();join();
  }
  // Exercise real queue backpressure beyond one cancellation-probe slice.
  {
    auto hardware=std::make_unique<Hardware>();
    auto backend=audio::create_realtime_pcm_backend(std::move(hardware),runtime,"audio",error);
    assert(backend->enable_output(true));assert(backend->pause_output(true));settle_idle();
    for(unsigned i=0;i<16;++i)assert(backend->write_output(samples.data(),128,32767,0));
    struct Resume {audio::PcmBackend *backend;bool cancel=false;} resume{backend.get()};
    const auto release=[&]{host_test::pause_ms(35);
      if(resume.cancel)assert(resume.backend->enable_output(false));else assert(resume.backend->pause_output(false));};
    host_test::Thread thread(release);
    int16_t packet[320]{};const auto failures=backend->diagnostics().queue_timeouts;
    assert(backend->write_output(packet,320,32767,120));
    thread.join();
    await([&]{return backend->diagnostics().completed_frames==2048+320;});
    assert(backend->diagnostics().queue_timeouts==failures);
    assert(backend->pause_output(true));settle_idle();
    for(unsigned i=0;i<15;++i)assert(backend->write_output(samples.data(),128,32767,0));
    const auto submitted=backend->diagnostics().submitted_frames;const auto start=host_test::now_ms();
    assert(!backend->write_output(packet,320,32767,30));
    assert(host_test::now_ms()-start>=20&&host_test::now_ms()-start<500);
    assert(backend->diagnostics().submitted_frames==submitted+128);
    assert(backend->diagnostics().queue_timeouts==failures+1);
    // A cancellation during a sliced wait is not a queue/hardware timeout.
    resume.cancel=true;host_test::Thread cancel_thread(release);
    assert(!backend->write_output(packet,320,32767,120));
    cancel_thread.join();
    assert(backend->diagnostics().queue_timeouts==failures+1);
    assert(backend->pause_output(false));
    while(!backend->shutdown(error))host_test::pause_ms(1);
    backend.reset();join();assert(queues==0);
  }
  // Unprofiled write cadence retains a real producer gap through the terminal
  // fade, and does not count a new output generation or pause as a dropout.
  {
    auto hardware=std::make_unique<Hardware>();auto *hw=hardware.get();
    auto backend=audio::create_realtime_pcm_backend(std::move(hardware),runtime,"audio",error);assert(backend);
    assert(backend->enable_output(true));assert(backend->write_output(samples.data(),128,32767,12));
    await([&]{return backend->diagnostics().completed_frames==128;});
    clock_offset_us.fetch_add(40000);
    assert(backend->write_output(samples.data(),128,32767,12));
    await([&]{return backend->diagnostics().completed_frames==256;});
    const auto before=backend->diagnostics();
    assert(before.maximum_write_start_gap_us>=40000&&before.maximum_write_start_gap_at_us>0);
    assert(backend->enable_output(false));await([&]{return !hw->enabled;});
    assert(backend->diagnostics().maximum_write_start_gap_us==before.maximum_write_start_gap_us);
    clock_offset_us.fetch_add(1000000);
    assert(backend->enable_output(true));assert(backend->write_output(samples.data(),128,32767,12));
    await([&]{return backend->diagnostics().completed_frames==384;});
    assert(backend->diagnostics().maximum_write_start_gap_us==0);
    assert(backend->pause_output(true));clock_offset_us.fetch_add(1000000);assert(backend->pause_output(false));
    assert(backend->write_output(samples.data(),128,32767,12));
    await([&]{return backend->diagnostics().completed_frames==512;});
    assert(backend->diagnostics().maximum_write_start_gap_us==0);
    while(!backend->shutdown(error))host_test::pause_ms(1);
    backend.reset();join();assert(queues==0);
  }
  // Hardware failure discards queued frames, fades/mutes and can still retire.
  auto hardware=std::make_unique<Hardware>();auto *observed=hardware.get();observed->fail_write=true;
  auto backend=audio::create_realtime_pcm_backend(std::move(hardware),runtime,"audio",error);assert(backend);
  assert(backend->enable_output(true));assert(backend->write_output(samples.data(),128,32767,12));
  await([&]{return backend->diagnostics().output_write_failures>0;});
  await([&]{return !observed->enabled.load();});
  while(!backend->shutdown(error))host_test::pause_ms(1);
  backend.reset();join();assert(queues==0);
  // Normal idle retirement must release the worker and hardware while keeping
  // this backend usable; explicit shutdown is not the only recovery path.
  hardware=std::make_unique<Hardware>();observed=hardware.get();
  backend=audio::create_realtime_pcm_backend(std::move(hardware),runtime,"audio",error);
  assert(backend && !task.active && queues==0);
  for(unsigned cycle=0;cycle<20;++cycle) {
    const auto releases=observed->idle_releases.load();
    assert(backend->enable_input(true));assert(backend->prepare_output());await([]{return task.waits>0;});
    assert(backend->enable_output(true));
    assert(backend->write_output(samples.data(),128,32767,12));
    const auto completed=(cycle+1)*128;
    await([&]{return backend->diagnostics().completed_frames==completed;});
    assert(backend->enable_output(false));await([&]{return !observed->enabled.load();});
    assert(task.active); // the output owner's existing five-second grace
    assert(backend->enable_input(false));host_test::pause_ms(10);
    task.hold_wrapper=true;
    clock_offset_us+=6000000;
    assert(backend->enable_input(false));await([]{return task.returned.load();});
    assert(observed->idle_releases>releases && queues==2);
    assert(!backend->enable_output(true)); // wrapper still owns the old handle
    task.hold_wrapper=false;await([]{return !task.active.load();});join();
  }
  assert(backend->shutdown(error) && queues==0);
  assert(!backend->enable_input(true) && !backend->enable_output(true));
  backend.reset();
  // A live microphone may keep duplex DMA while an unused output worker
  // retires. Renewal during a long real turn postpones that retirement.
  hardware=std::make_unique<Hardware>();observed=hardware.get();
  backend=audio::create_realtime_pcm_backend(std::move(hardware),runtime,"audio",error);
  assert(backend->enable_input(true)&&!task.active&&queues==0);
  assert(backend->prepare_output());await([]{return task.waits>0;});
  clock_offset_us+=4000000;assert(backend->prepare_output());host_test::pause_ms(10);
  clock_offset_us+=2000000;assert(backend->enable_input(true));host_test::pause_ms(10);
  assert(task.active&&observed->input&&!observed->enabled);
  clock_offset_us+=4000000;assert(backend->enable_input(true));await([]{return task.returned&&!task.active;});
  assert(observed->input&&observed->idle_releases==0);join();
  assert(backend->enable_input(false)&&observed->idle_releases>0);
  assert(backend->shutdown(error));backend.reset();assert(queues==0);
  // Capture demand must not turn a failed output stop into a successful
  // retirement: keep the worker until the physical output really disables.
  hardware=std::make_unique<Hardware>();observed=hardware.get();
  backend=audio::create_realtime_pcm_backend(std::move(hardware),runtime,"audio",error);
  assert(backend->enable_input(true)&&backend->enable_output(true));
  assert(backend->write_output(samples.data(),128,32767,12));
  await([&]{return backend->diagnostics().completed_frames==128;});
  observed->fail_disable=true;assert(backend->enable_output(false));
  await([&]{return backend->diagnostics().output_muted;});
  clock_offset_us+=6000000;assert(backend->enable_input(true));host_test::pause_ms(10);
  assert(task.active&&!task.returned&&observed->enabled&&observed->input);
  observed->fail_disable=false;assert(backend->enable_input(true));
  await([&]{return !observed->enabled.load();});
  clock_offset_us+=6000000;assert(backend->enable_input(true));
  await([]{return task.returned&&!task.active;});join();
  assert(observed->input&&observed->idle_releases==0);
  assert(backend->enable_input(false)&&observed->idle_releases>0);
  assert(backend->shutdown(error));backend.reset();assert(queues==0);
  hardware=std::make_unique<Hardware>();observed=hardware.get();
  backend=audio::create_realtime_pcm_backend(std::move(hardware),runtime,"audio",error);
  assert(backend->enable_output(true));
  observed->hold_write=true;
  int16_t handoff_pcm[320]{};
  assert(backend->write_output(handoff_pcm,320,32767,10));
  await([&]{return observed->in_write.load();});
  assert(!backend->quiesce(error) && error=="audio_foreground_stop_pending");
  assert(!backend->quiesce(error)); // old hardware write is still in flight
  observed->fail_disable=true;observed->hold_write=false;
  await([&]{return backend->diagnostics().output_muted;});
  assert(!backend->quiesce(error) && observed->enabled);
  observed->fail_disable=false;
  // Polling retirement every millisecond repeatedly notifies the worker. Its
  // 2 ms fade deadline must advance through those wakes, rather than restart.
  await([&]{return backend->quiesce(error);});
  assert(!observed->enabled && backend->diagnostics().pending_frames==0);
  while(!backend->shutdown(error))host_test::pause_ms(1);
  backend.reset();join();assert(queues==0);
  std::cout<<"PCM owner: lazy capture, turn prewarm, full capacity, fades, failures, 20 shutdown and 20 idle restart cycles passed\n";
}
