#pragma once
#include "graphics/scene.h"
#include <memory>
#include <string>

namespace lunaredu::lunaros::graphics {
// Copy into immutable render snapshots; the runtime remains the authority.
struct Page { uint64_t generation=0; bool widgets=false; };
struct InputStats {
  uint32_t coalesced_moves=0,dropped_pointers=0,queue_high_water=0,cancelled_contacts=0;
  // Accepted events only. Queue residence, not input-to-photon latency.
  uint64_t queue_time_ms=0;
  uint32_t consumed_events=0,queue_max_ms=0,over_16_ms=0,over_32_ms=0;
  uint32_t dropped_taps=0;
};
struct Presentation {
  void *context=nullptr;
  bool (*submit)(void *,std::string &)=nullptr;
  bool (*wait_idle)(void *,std::string &)=nullptr;
};
struct Renderer {
  void *context=nullptr;
  bool (*render)(void *,Scene &,uint64_t,Scene::Emit,void *)=nullptr;
};

// One runtime per screen. Input methods run on the application/input owner;
// scene/render/service methods run on the existing managed render owner.
// The OS owns queues, contact qualification, generations and first-frame gating.
class SceneRuntime {
 public:
  using Clock=uint32_t (*)(); // Monotonic milliseconds, unsigned wrap supported.
  explicit SceneRuntime(Clock clock=nullptr);
  ~SceneRuntime();
  SceneRuntime(const SceneRuntime &)=delete;
  SceneRuntime &operator=(const SceneRuntime &)=delete;

  // Input owner. key/revision express page identity, never a widget pointer.
  Page select(uint64_t key,bool widgets,uint64_t revision=0);
  Page page() const;
  void invalidate();
  bool cancel_contact();
  bool pointer(bool pressed,int x,int y,bool valid=true,bool reserved=false);
  bool tap(int x,int y);
  // Input owner queues a generation-qualified intent. Only service() on the
  // render owner touches the opt-in LVGL Group; no widget pointers cross owners.
  bool focus(FocusCommand);
  bool take_action(Action &);
  bool ready() const;
  unsigned dropped_actions() const;
  InputStats input_stats() const;

  // Render owner. The caller only builds product-specific page content.
  bool begin(Page,uint16_t background,bool antialias);
  Scene &scene();
  // Render owner qualifies transformed content; input reads an atomic flag.
  void presenting_motion(bool active);
  bool take_motion_finish_request();
  bool render(RasterOutput);
  // Synchronous borrowed renderer; no Desktop policy enters the runtime.
  bool render_with(Page,Renderer);
  bool present(Page,Presentation,std::string &error);
  void suspend(Page);
  // Raw Launcher/App frames reuse the same render-owner engine without keeping
  // an old widget tree or scheduling its input/timers.
  void prepare_canvas(Page);
  bool service();
  // UINT32_MAX means no timed work; input/render notifications still wake owner.
  uint32_t next_wake_ms() const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace lunaredu::lunaros::graphics
