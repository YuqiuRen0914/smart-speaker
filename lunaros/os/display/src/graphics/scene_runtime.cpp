#include "graphics/scene_runtime.h"
#include "queue.h"
#include <atomic>
#include <algorithm>
#include <limits>
#include <mutex>
#include <chrono>

namespace lunaredu::lunaros::graphics {
struct SceneRuntime::Impl {
  struct Pointer {uint64_t epoch;int x,y;bool pressed,valid,move=false;uint32_t queued_at=0;};
  // Only adjacent move samples coalesce. Down/up remain ordered, including a
  // contact which begins and ends while the render owner is busy. The lock
  // covers a small value copy only; it never covers LVGL, allocations or I/O.
  struct Pointers {
    std::mutex mutex;std::array<Pointer,16> values{};unsigned read=0,count=0;
    std::atomic<uint32_t> coalesced{0},dropped{0},high_water{0};
    bool push(Pointer p){
      std::lock_guard<std::mutex> lock(mutex);
      if(count&&p.move){auto &last=values[(read+count-1)%values.size()];if(last.move&&last.epoch==p.epoch){last=p;++coalesced;return true;}}
      if(count==values.size()){++dropped;return false;}
      values[(read+count)%values.size()]=p;++count;
      if(count>high_water.load())high_water.store(count);
      return true;
    }
    bool pop(Pointer &p){std::lock_guard<std::mutex> lock(mutex);if(!count)return false;p=values[read];read=(read+1)%values.size();--count;return true;}
  };
  struct Tap {uint64_t epoch;int x,y;uint32_t queued_at;};
  struct Focus {uint64_t epoch;FocusCommand command;uint32_t queued_at;};
  Clock clock;
  explicit Impl(Clock clock):clock(clock){}
  std::atomic<uint64_t> queue_time_ms{0};
  std::atomic<uint32_t> consumed_events{0},queue_max_ms{0},over_16_ms{0},over_32_ms{0};
  void consumed(uint32_t queued_at){
    const uint32_t age=clock()-queued_at;
    ++consumed_events;queue_time_ms.fetch_add(age);
    if(age>16)++over_16_ms;
    if(age>32)++over_32_ms;
    auto previous=queue_max_ms.load();
    while(previous<age&&!queue_max_ms.compare_exchange_weak(previous,age)){}
  }
  Scene scene;
  Pointers pointers;
  Queue<Tap> taps;
  Queue<Focus,8> focus_commands;
  Queue<Action> actions;
  std::atomic<uint64_t> generation{1},shown{0};
  std::atomic<bool> cancel{false},redraw{false},reset_input{false},motion{false};
  std::atomic<bool> finish_motion{false};
  std::atomic<uint32_t> cancelled_contacts{0};
  std::atomic<unsigned> dropped{0};
  std::atomic<uint32_t> dropped_taps{0};
  // Input-owner state, never read by rendering.
  uint64_t key=0,revision=0;
  std::atomic<bool> widgets{false};
  bool contact=false,blocked=false;
  int x=0,y=0;
  // Render-owner state, never read by input.
  Page rendering{};
  bool rendered=false;
  uint32_t wake=UINT32_MAX;
  bool current(Page p) const {return p.generation==generation.load(std::memory_order_acquire);}
  bool ready() const {return !motion.load(std::memory_order_acquire)&&shown.load(std::memory_order_acquire)==generation.load(std::memory_order_acquire);}
  static void emitted(void *context,Action a){
    auto &s=*static_cast<Impl *>(context);
    if(!s.actions.push(a))s.dropped.fetch_add(1,std::memory_order_relaxed);
  }
  void reset_contact(){if(reset_input.exchange(false)){blocked=blocked||contact;contact=false;}}
  void failed(){
    // Render failures revoke the generation atomically. Input-owner fields are
    // reset on its next call; neither side mutates the other's ordinary fields.
    auto expected=rendering.generation;
    (void)generation.compare_exchange_strong(expected,expected+1,std::memory_order_acq_rel);
    shown.store(0,std::memory_order_release);rendered=false;
    scene.clear();reset_input.store(true);cancel.store(true);redraw.store(true);
  }
};
SceneRuntime::SceneRuntime(Clock clock):impl_(std::make_unique<Impl>(clock?clock:[]()->uint32_t{
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
})){}
SceneRuntime::~SceneRuntime()=default;
Page SceneRuntime::page() const {return {impl_->generation.load(std::memory_order_acquire),impl_->widgets};}
Page SceneRuntime::select(uint64_t key,bool widgets,uint64_t revision){
  impl_->reset_contact();
  if(impl_->key!=key||impl_->widgets!=widgets||impl_->revision!=revision){
    impl_->key=key;impl_->widgets=widgets;impl_->revision=revision;invalidate();
  }
  return page();
}

void SceneRuntime::invalidate(){
  if(impl_->contact)++impl_->cancelled_contacts;
  impl_->generation.fetch_add(1,std::memory_order_acq_rel);
  impl_->shown.store(0,std::memory_order_release);
  impl_->finish_motion.store(false,std::memory_order_release);
  impl_->blocked=impl_->blocked||impl_->contact;
  impl_->contact=false;impl_->cancel.store(true);impl_->redraw.store(true);
}
bool SceneRuntime::cancel_contact(){if(!impl_->contact)return false;invalidate();return true;}
bool SceneRuntime::ready() const {return impl_->ready();}
unsigned SceneRuntime::dropped_actions() const {return impl_->dropped.load(std::memory_order_relaxed);}
InputStats SceneRuntime::input_stats() const {return {impl_->pointers.coalesced.load(),impl_->pointers.dropped.load(),impl_->pointers.high_water.load(),impl_->cancelled_contacts.load(),impl_->queue_time_ms.load(),impl_->consumed_events.load(),impl_->queue_max_ms.load(),impl_->over_16_ms.load(),impl_->over_32_ms.load(),impl_->dropped_taps.load()};}
bool SceneRuntime::pointer(bool pressed,int x,int y,bool valid,bool reserved){
  impl_->reset_contact();
  const auto epoch=impl_->generation.load(std::memory_order_acquire);
  bool queued=false;
  if(!pressed){
    if(impl_->contact){
      queued=impl_->pointers.push({epoch,impl_->x,impl_->y,false,valid&&!reserved,false,impl_->clock()});
      if(!queued)invalidate();
    }
    impl_->contact=impl_->blocked=false;
  }else if(valid&&!reserved&&impl_->widgets&&impl_->motion.load()&&
      impl_->shown.load()==epoch&&!impl_->blocked){
    // Ask the render owner to settle first. Keep the held point eligible on
    // its next sample, after the untransformed frame is acknowledged.
    impl_->finish_motion.store(true);return false;
  }else if(!valid||reserved||!impl_->ready()||!impl_->widgets){
    if(impl_->contact)invalidate();
    impl_->blocked=true;
  }else if(!impl_->blocked){
    impl_->x=x;impl_->y=y;
    queued=impl_->pointers.push({epoch,x,y,true,true,impl_->contact,impl_->clock()});
    if(!queued){invalidate();impl_->blocked=true;}
    else impl_->contact=true;
  }
  return queued;
}
bool SceneRuntime::tap(int x,int y){
  if(!impl_->widgets||!impl_->ready())return false;
  if(impl_->taps.push({impl_->generation.load(std::memory_order_acquire),x,y,impl_->clock()}))return true;
  ++impl_->dropped_taps;return false;
}
bool SceneRuntime::focus(FocusCommand command){
  if(unsigned(command)>unsigned(FocusCommand::Activate))return false;
  impl_->reset_contact();
  if(!impl_->widgets.load(std::memory_order_acquire)||!impl_->ready()||impl_->contact)return false;
  const auto generation=impl_->generation.load(std::memory_order_acquire);
  if(!impl_->focus_commands.push({generation,command,impl_->clock()})){
    invalidate();return false;
  }
  impl_->redraw.store(true,std::memory_order_release);return true;
}
bool SceneRuntime::take_action(Action &a){
  while(impl_->actions.pop(a))if(impl_->ready()&&a.epoch==impl_->generation.load(std::memory_order_acquire))return true;
  return false;
}
bool SceneRuntime::begin(Page p,uint16_t background,bool antialias){
  if(!p.widgets||!impl_->current(p))return false;
  impl_->rendering=p;impl_->rendered=false;
  if(!impl_->scene.begin(p.generation,background,antialias,Impl::emitted,impl_.get())){impl_->failed();return false;}
  return true;
}
Scene &SceneRuntime::scene(){return impl_->scene;}
void SceneRuntime::presenting_motion(bool active){impl_->motion.store(active,std::memory_order_release);}
bool SceneRuntime::take_motion_finish_request(){return impl_->finish_motion.exchange(false);}
bool SceneRuntime::render(RasterOutput output){
  if(!impl_->current(impl_->rendering))return false;
  impl_->rendered=impl_->scene.render(output);
  if(!impl_->rendered)impl_->failed();
  return impl_->rendered;
}
bool SceneRuntime::render_with(Page p,Renderer renderer){
  if(!p.widgets||!impl_->current(p))return false;
  impl_->rendering=p;
  impl_->rendered=renderer.render && renderer.render(renderer.context,impl_->scene,p.generation,Impl::emitted,impl_.get());
  if(!impl_->rendered)impl_->failed();
  return impl_->rendered;
}
bool SceneRuntime::present(Page p,Presentation output,std::string &error){
  if(!p.widgets||!impl_->current(p)||p.generation!=impl_->rendering.generation||!impl_->rendered){
    error="No current rendered widget frame";return false;
  }
  if(!output.submit||!output.wait_idle){error="Incomplete graphics presentation binding";impl_->failed();return false;}
  if(!output.submit(output.context,error)){impl_->failed();return false;}
  if(impl_->shown.load(std::memory_order_acquire)!=p.generation){
    if(!output.wait_idle(output.context,error)){impl_->failed();return false;}
    if(!impl_->current(p)){error="Graphics page changed during presentation";return false;}
    impl_->shown.store(p.generation,std::memory_order_release);
  }
  return true;
}
void SceneRuntime::suspend(Page p){
  if(!impl_->current(p))return;
  impl_->shown.store(0,std::memory_order_release);impl_->scene.suspend();
  impl_->rendering={};impl_->rendered=false;impl_->wake=UINT32_MAX;
}
void SceneRuntime::prepare_canvas(Page p){
  if(!impl_->current(p))return;
  if(impl_->rendering.generation!=p.generation)impl_->scene.clear();
  impl_->shown.store(0,std::memory_order_release);impl_->rendering=p;
  impl_->rendered=false;impl_->wake=UINT32_MAX;
}
bool SceneRuntime::service(){
  bool changed=impl_->redraw.exchange(false);
  if(impl_->cancel.exchange(false)){
    // A freshly built generation has already destroyed the old contact tree.
    // Never reset its input device after first-frame confirmation.
    if(!impl_->current(impl_->rendering))
      (void)impl_->scene.pointer(impl_->rendering.generation,false,0,0,false);
  }
  Impl::Pointer p;
  while(impl_->pointers.pop(p))if(impl_->ready()&&p.epoch==impl_->generation.load(std::memory_order_acquire)){
    impl_->consumed(p.queued_at);
    changed|=impl_->scene.pointer(p.epoch,p.pressed,p.x,p.y,p.valid);
  }
  Impl::Tap t;
  while(impl_->taps.pop(t))if(impl_->ready()&&t.epoch==impl_->generation.load(std::memory_order_acquire)){
    impl_->consumed(t.queued_at);
    changed|=impl_->scene.tap(t.epoch,t.x,t.y);
  }
  Impl::Focus focus;
  while(impl_->focus_commands.pop(focus))if(impl_->ready()&&focus.epoch==impl_->generation.load(std::memory_order_acquire)){
    impl_->consumed(focus.queued_at);
    changed|=impl_->scene.focus(focus.command);
  }
  if(impl_->current(impl_->rendering)&&impl_->rendered){
    const auto update=impl_->scene.service();impl_->wake=update.next_ms;changed|=update.changed;
  }else impl_->wake=UINT32_MAX;
  return changed;
}
uint32_t SceneRuntime::next_wake_ms() const {return impl_->wake;}
} // namespace lunaredu::lunaros::graphics
