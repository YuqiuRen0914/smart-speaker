#pragma once
#include "lunaredu_pet_desktop_entry.h"
#include "lunaredu_pet_scene.h"
#include "desktop/ui/render_hooks.h"
#include "display/presentation_completion.h"
#include <atomic>

namespace lunaredu::pet {
struct DesktopSceneStatus {uint32_t generation=0,pool_busy=0,last_raster_us=0;};
class DesktopSceneSurface;
struct DesktopScenePresentation {
  DesktopSceneSurface *owner=nullptr;
  Scene scene;
  uint64_t activation=0;
  void paint(desktop::UiPaintPort &,bool) const;
  desktop::UiRenderCache render_cache() const;
};
// Main owns publish/capture/touch and activation resets. The existing renderer
// owns paint/cache; physical ACKs use the display owner. No task or frame pool.
// Desktop currently supplies a square canvas; the product chooses its size.
class DesktopSceneSurface {
 public:
  explicit DesktopSceneSurface(uint16_t side):side_(side) {}
  DesktopSceneSurface(const DesktopSceneSurface &)=delete;
  DesktopSceneSurface &operator=(const DesktopSceneSurface &)=delete;
  void publish(Scene scene){latest_=std::move(scene);}
  DesktopScenePresentation capture(){return {this,latest_,activation_.load(std::memory_order_acquire)};}
  void clear();
  void presented(display::ScreenFrameView);
  void unchanged(display::FrameCanvas);
  void touch(bool system_owned,bool valid,bool pressed,uint16_t x,uint16_t y,unsigned rotation,uint32_t now);
  DesktopSceneStatus status() const;
 private:
  friend struct DesktopScenePresentation;
  void paint(const Scene &,uint64_t activation,desktop::UiPaintPort &);
  desktop::UiRenderCache cache();
  const uint16_t side_;
  Scene latest_;
  bool touch_down_=false;
  uint16_t touch_x_=0,touch_y_=0;
  struct PaintBaseline {display::Damage visible{};uint64_t signature=0;unsigned rotation=0;bool valid=false;} baseline_;
  display::PresentationCompletion<> completion_;
  std::mutex presentation_mutex_;
  std::atomic<uint64_t> activation_{1};
  std::atomic<uint32_t> raster_us_{0};
  std::atomic<uint64_t> presented_revision_{0};
};
template<DesktopSceneSurface *Surface> struct DesktopSceneEntry : DesktopEntry {
  using Presentation=DesktopScenePresentation;
  template<class Host> class Session : public lunaros::DesktopSessionDefaults {
   public:
    explicit Session(Host &host):host_(host) {}
    std::string running_application() const{return host_.state().page==DesktopDescription::route()?kApplicationId:"";}
    void poll(bool allowed){if(allowed&&host_.state().page==DesktopDescription::route()&&host_.take_redraw())host_.publish_application(host_.model(),Host::Paint::Full);}
    void cancel_input(){Surface->touch(true,false,false,0,0,0,0);}
   private: Host &host_;
  };
  template<class Bindings> static typename Bindings::Definition binding(){
    return {kApplicationId,
      [](typename Bindings::CaptureContext &)->typename Bindings::Presentation{return Surface->capture();},
      [](typename Bindings::InputContext &c){
        // Primary dispatch gives system/shell controls first refusal. It is
        // not a new touch sample and must not cancel an App-owned contact.
        if(c.primary){
          if(c.top_owned||c.edge_back||c.system_owned())Surface->touch(true,false,false,0,0,0,0);
          return false;
        }
        if(c.back||c.edge_back){Surface->touch(true,false,false,0,0,0,0);c.show_shell();return true;}
        const auto &t=c.model.touch;
        Surface->touch(c.top_owned||c.system_owned(),t.valid,t.pressed,t.x,t.y,
            c.ui_state().desktop.preferences.rotation,uint32_t(c.now_us/1000));
        c.host.record_input(c.model);
        return true;
      },
      [](typename Bindings::EntryContext &c){c.enter_ui({DesktopDescription::route()});},
      [](typename Bindings::LaunchContext &c,std::string &error){return c.host.applications().switch_to(kApplicationId,error)&&c.enter_ui({DesktopDescription::route()});}};
  }
};
}
