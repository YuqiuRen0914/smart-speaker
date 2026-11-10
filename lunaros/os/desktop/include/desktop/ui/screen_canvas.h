#pragma once
#include "display/managed_screen.h"
#include "display/canvas_retention.h"
#include "desktop/ui/screen_paint_port.h"
#include "display/pipeline/pixel_orientation.h"
#include "desktop/ui/status_bar.h"
#include "desktop/ui/text_painter.h"
#include "desktop/navigation/compositor.h"
#include "desktop/graphics/canvas.h"
#include "desktop/graphics/desktop.h"
#include "desktop/graphics/canvas_spec.h"
#include "desktop/navigation/page_motion.h"
#include "desktop/ui/internal/chrome_coverage.h"
#include <new>

namespace lunaredu::desktop {
struct CanvasPresentation {
  const DesktopShell *shell = nullptr;
  DesktopStatus status;
  ::lunaredu::lunaros::raster::RenderPolicy policy;
  lunaros::graphics::Page page;
  unsigned rotation = 0;
  bool incremental = false, overlays = true, full = true;
  std::string_view application;
  uint64_t motion_key=0;
  bool motion_enabled=false,launcher=true;
};
// App painters borrow this canvas. Frame ownership, recovery, navigation
// composition, rotation and generation-checked submission remain in OS.
template<class Job> class ScreenCanvas {
 public:
  explicit ScreenCanvas(display::ManagedScreen<Job> &screen) : screen_(screen) {
    service_bound_ = screen_.render_service({this,[](void *p){return static_cast<ScreenCanvas *>(p)->service();},
      [](void *p){auto &s=*static_cast<ScreenCanvas *>(p);return std::min(s.graphics_.next_wake_ms(),s.motion_.next_wake_ms(s.now()));}});
  }
  bool service_bound() const { return service_bound_; }
  bool configure(DesktopRenderOptions options) {
    if (!service_bound_ || screen_.lifecycle() != display::ScreenLifecycle::Unconfigured) return false;
    options_ = options;
    if(!graphics_.scene().configure(kDesktopCanvas))return false;
    // Optional acceleration metadata follows the normal heap placement policy,
    // outside the always-resident screen owner. OOM retains exact comparison.
    raster_coherence_.reset(new(std::nothrow) std::array<lunaros::graphics::RasterCoherence,2>());
    return true;
  }
  lunaros::graphics::SceneRuntime &graphics() { return graphics_; }
  DesktopNavigationCompositor &navigation() { return navigation_; }
  void configure(CanvasPresentation presentation) {
    presentation_ = presentation;
    graphics_.scene().text_scale(lunaros::graphics::metrics::text_scale_percent(presentation.shell?presentation.shell->preferences.text_size:1));
    const bool enabled=presentation.motion_enabled&&options_.home_card_animation&&presentation.overlays&&
        presentation.shell&&!presentation.shell->home_navigation.contact&&!presentation.shell->home_navigation.moving;
    if(motion_.select(presentation.motion_key,presentation.launcher,enabled,now())){motion_background_=false;motion_cached_=false;motion_deferred_=false;}
    if(!motion_.active()){graphics_.scene().cancel_animation();motion_cached_=false;motion_deferred_=false;motion_shell_.reset();cached_.shell=nullptr;}
    graphics_.scene().motion(presentation.motion_enabled);
    graphics_.presenting_motion(motion_.active());
  }
  bool defer_motion_repaint(){
    // The finite launch preview already owns its pixels. Ordinary updates on
    // this same page are refreshed when it settles; a new page, system frame,
    // gesture, disabled motion or recovery still paints immediately.
    if(!motion_.active()||!motion_cached_||screen_.recovery_pending()||
        cached_.page.generation!=presentation_.page.generation||
        presentation_.status.peer_severity==DesktopStatus::Severity::Critical||
        presentation_.status.peer_severity==DesktopStatus::Severity::Conflict)return false;
    motion_deferred_=true;return true;
  }
  bool begin(uint16_t width,uint16_t height,uint16_t color,uint16_t y=0,bool clear=true) {
    have_damage_map_=false;
    pixels_=nullptr; width_=height_=0; std::string error;
    if (screen_.width()!=kDesktopCanvas.width || screen_.height()!=kDesktopCanvas.height ||
        (options_.home_card_animation && !screen_.auxiliary_frame(1))) {
      screen_.request_redraw(); return false;
    }
    navigation_.bind(options_.home_card_animation ? screen_.auxiliary_frame(0) : nullptr,
        options_.home_card_animation ? screen_.auxiliary_frame(1) : nullptr);
    auto result=screen_.begin_frame(width,height,color,y,clear,error);
    if (!result.pixels) { screen_.request_redraw(); return false; }
    frame_=result; pixels_=result.pixels; width_=width; height_=height;
    retention_.begin(result,presentation_.page.generation,screen_.ownership_generation());
    if(auto *token=coherence())token->invalidate();
    return true;
  }
  auto surface() {
    retention_.invalidate();
    // Raw App access has no complete damage declaration. Reuse must fall back
    // to exact comparison until the raster owner establishes a new baseline.
    if(auto *token=coherence())token->invalidate();
    return chrome_surface();
  }
  display::FrameCanvas submission_canvas() const { return frame_; }
  // System-owned chrome has complete coverage recorded by flush(). App raw
  // painters must use surface()/pixels(), which invalidate target coherence.
  auto chrome_surface(){return lunaros::graphics::canvas::Surface{pixels_,width_,height_,kDesktopCanvas.stride,presentation_.policy.antialias};}
  auto retained_surface(const void *owner,uint64_t key) {
    const bool eligible=!presentation_.full&&!motion_.active()&&!(presentation_.rotation&3)&&
        width_==kDesktopCanvas.width&&height_==kDesktopCanvas.height;
    return retention_.borrow(owner,key,eligible);
  }
  uint16_t *chrome_pixels(){return pixels_;}
  // Mailbox jobs release their snapshot after paint. Scheduled motion uses
  // cached_ and its owned shell/text, never this borrowed presentation.
  void end_frame(){presentation_={};pixels_=nullptr;width_=height_=0;}
  uint16_t *pixels() {retention_.invalidate();if(auto *token=coherence())token->invalidate();return pixels_;}
  uint16_t width() const { return width_; }
  uint16_t height() const { return height_; }
  void reject_frame(){screen_.invalidate_frame();screen_.request_redraw();}
  void rotate(unsigned rotation) {
    if(rotation&3){if(auto *token=coherence())token->invalidate();}
    if(rotation&3)retention_.invalidate();
    display::rotate_rgb565(pixels_,rotation,kDesktopCanvas.width);
  }
  bool flush(uint16_t x,uint16_t y,bool capture=true) {
    if (!presentation_.shell) return false;
    std::string error;
    auto &screen=screen_;
    const bool incremental=presentation_.incremental && x==0 && y==0 && width_==kDesktopCanvas.width && height_==kDesktopCanvas.height;
    rotations_[screen.active_slot()]=presentation_.rotation;
    display::ScreenRegion region{x,y,width_,height_};
    bool composed_full=false;
    if(motion_.active()&&width_==kDesktopCanvas.width&&height_==kDesktopCanvas.height&&x==0&&y==0){
      composed_full=true;
      auto *source=screen_.auxiliary_frame(0),*background=screen_.auxiliary_frame(1);
      if(capture)std::copy_n(screen.active_frame(),kDesktopCanvas.pixels(),source);
      if(motion_.opening()&&!motion_background_){
        auto desktop=std::make_unique<DesktopShell>(*presentation_.shell);
        desktop->surface=DesktopSurface::Launcher;desktop->home_navigation.reset();desktop->settle_motion();
        const auto tile=desktop->tile(desktop->focus);motion_target_={tile.x,tile.y,tile.diameter};
        for(unsigned i=0;i<desktop->preferences.rotation;++i){const float x=motion_target_.x;motion_target_.x=canvas_layout::last-motion_target_.y;motion_target_.y=x;}
        for(unsigned i=0;i<presentation_.rotation;++i){const float x=motion_target_.x;motion_target_.x=motion_target_.y;motion_target_.y=canvas_layout::last-x;}
        if(!draw_launcher(*desktop,presentation_.status,background,screen_text_painter(),false,nullptr,&graphics_.scene())){
          screen_.invalidate_frame();screen_.request_redraw();return false;
        }
        motion_background_=true;
      }
      if(capture){
        if(!motion_shell_)motion_shell_=std::make_unique<DesktopShell>();
        *motion_shell_=*presentation_.shell;motion_location_=presentation_.status.location?presentation_.status.location:"";
        motion_network_=presentation_.status.network?presentation_.status.network:"";motion_application_=presentation_.application;
        cached_=presentation_;cached_.shell=motion_shell_.get();cached_.status.location=motion_location_.c_str();cached_.status.network=motion_network_.c_str();cached_.application=motion_application_;
        motion_cached_=true;
      }
      // The first-frame work and wallpaper fitting do not consume animation time.
      motion_.prepared(now());
      if(!screen.timed(display::ScreenPipelinePhase::Compose,[&]{return motion_.compose(screen.active_frame(),source,background,motion_target_,presentation_.rotation,presentation_.policy.antialias,now(),&graphics_.scene());})){
        screen_.invalidate_frame();screen_.request_redraw();return false;
      }
      // App motion stays below the OS chrome, including its hidden-bar handle.
      // Both frames are already oriented; borrow the current overlay pixels.
      const int chrome_height=presentation_.shell->status_navigation.showing(presentation_.shell->preferences.auto_hide_status)?status_layout::height:lunaros::graphics::metrics::inset;
      for(int cy=0;cy<chrome_height;++cy)for(int cx=0;cx<canvas_layout::side;++cx){
        const auto index=display::rotated_pixel_index(cx,cy,presentation_.rotation,canvas_layout::side);
        screen.active_frame()[index]=source[index];
      }
    }
    bool composition_failed=false;
    if(navigation_.compose(*presentation_.shell,presentation_.status,screen_text_painter(),screen.active_frame(),presentation_.rotation,presentation_.overlays,&graphics_.scene(),&composition_failed)){
      composed_full=true;
      region={0,0,kDesktopCanvas.width,kDesktopCanvas.height};
      have_damage_map_=false;
    }
    if(composition_failed){screen_.invalidate_frame();screen_.request_redraw();return false;}
    const auto chrome=ChromeCoverage::capture(*presentation_.shell,presentation_.status,presentation_.overlays,presentation_.rotation);
    if(auto *token=coherence()){
      if(composed_full||(presentation_.rotation&3))token->invalidate();
      else if(token->owner)chrome.mark(token->overwritten);
    }
    // The first settled frame must erase any transformed content, even when
    // the App's ordinary damage declaration is smaller than that composition.
    if(composed_full||previous_composed_full_)have_damage_map_=false;
    if(have_damage_map_){previous_chrome_.mark(damage_map_);chrome.mark(damage_map_);}
    const auto submitted=screen.present(frame_,region,
      {incremental?presentation_.page.generation:0,presentation_.application},incremental&&!presentation_.full&&!motion_.active(),error,
      have_damage_map_&&!motion_.active()?&damage_map_:nullptr);
    display::FrameDamageMap overwritten(kDesktopCanvas.width,kDesktopCanvas.height);chrome.mark(overwritten);
    retention_.finish(overwritten.bounds(),submitted!=display::FrameSubmission::Failed&&!composed_full&&!(presentation_.rotation&3));
    if (submitted==display::FrameSubmission::Failed) { screen_.request_redraw(); return false; }
    previous_chrome_=chrome;previous_composed_full_=composed_full;
    return true;
  }
  bool begin_widget_target(uint16_t background) {
    auto *token=coherence();const auto previous=token?*token:lunaros::graphics::RasterCoherence{};
    if(!begin(kDesktopCanvas.width,kDesktopCanvas.height,background,0,false))return false;
    if(token)*token=previous;
    return true;
  }
  bool begin_widgets(uint16_t background) {
    return begin_widget_target(background)&&graphics_.begin(presentation_.page,background,presentation_.policy.antialias);
  }
  lunaros::graphics::RasterOutput widget_output(){
    return {pixels_,kDesktopCanvas.pixels(),true,coherence(),&damage_map_};
  }
  void widgets_rendered(){have_damage_map_=damage_map_.valid(kDesktopCanvas.width,kDesktopCanvas.height);}
  bool render_widgets() {
    const bool okay=screen_.timed(display::ScreenPipelinePhase::Raster,[&]{return graphics_.render(widget_output());});
    if(okay)widgets_rendered();
    return okay;
  }
  void present_widgets() {
    std::string error;
    if (!graphics_.present(presentation_.page,{this,
        [](void *p,std::string &error) {
          if (static_cast<ScreenCanvas *>(p)->flush(0,0)) return true;
          error="screen_submission_failed"; return false;
        },[](void *p,std::string &error) { return static_cast<ScreenCanvas *>(p)->screen_.wait_idle(error); }},error)) {
      screen_.invalidate_frame(); screen_.request_redraw();
    }
  }
  unsigned frame_rotation(const uint16_t *pixels) {
    const auto slot=screen_.frame_slot(pixels);
    return slot<2?rotations_[slot]:0;
  }
  void damage_map(const display::FrameDamageMap &map){damage_map_=map;have_damage_map_=map.valid(kDesktopCanvas.width,kDesktopCanvas.height);}
 private:
  std::unique_ptr<std::array<lunaros::graphics::RasterCoherence,2>> raster_coherence_;
  lunaros::graphics::RasterCoherence *coherence(){return raster_coherence_?&(*raster_coherence_)[screen_.active_slot()]:nullptr;}
  uint32_t now() const {return uint32_t(screen_.now_us()/1000);}
  bool service(){
    const bool was_active=motion_.active();bool changed=motion_.tick(now());
    if(graphics_.take_motion_finish_request()&&was_active){motion_.finish();changed=true;}
    if(!motion_.active())graphics_.scene().cancel_animation();
    if(was_active&&changed&&motion_cached_&&cached_.page.generation==graphics_.page().generation&&!screen_.recovery_pending()){
      // Timed frames borrow a fresh output slot and the stable source raster.
      // They do not round-trip through the input/product owner or touch the
      // immutable mailbox Job whose lifetime ended after its paint callback.
      presentation_=cached_;
      const bool ok=screen_.timed(display::ScreenPipelinePhase::Animation,[&]{
        screen_.begin_render();
        if(!begin(kDesktopCanvas.width,kDesktopCanvas.height,0,0,false))return false;
        // Active composition writes the entire output (background included).
        // Only the final settled frame needs the original untransformed copy.
        if(!motion_.active())std::copy_n(screen_.auxiliary_frame(0),kDesktopCanvas.pixels(),pixels_);
        if(!flush(0,0,false))return false;
        if(!motion_.active()){std::string error;return screen_.wait_idle(error);}
        return true;
      });
      if(ok)changed=false;
      else {screen_.invalidate_frame();motion_.finish();graphics_.scene().cancel_animation();motion_cached_=false;}
      if(!motion_.active()){
        if(motion_deferred_)changed=true;
        motion_deferred_=false;motion_cached_=false;motion_shell_.reset();cached_.shell=nullptr;presentation_.shell=nullptr;
      }
    }else if(was_active&&changed&&!motion_cached_){motion_.finish();graphics_.scene().cancel_animation();}
    graphics_.presenting_motion(motion_.active());
    return graphics_.service()||changed;
  }
  display::ManagedScreen<Job> &screen_;
  display::CanvasRetention retention_;
  CanvasPresentation presentation_;
  display::FrameDamageMap damage_map_;
  bool have_damage_map_=false;
  ChromeCoverage previous_chrome_;
  bool previous_composed_full_=false;
  DesktopNavigationCompositor navigation_;
  lunaros::graphics::SceneRuntime graphics_;
  display::FrameCanvas frame_;
  uint16_t *pixels_=nullptr;
  uint16_t width_=0,height_=0;
  unsigned rotations_[2]{};
  bool service_bound_ = false;
  DesktopRenderOptions options_;
  PageMotion motion_;
  HomeTarget motion_target_;
  bool motion_background_=false;
  bool motion_cached_=false;
  bool motion_deferred_=false;
  std::unique_ptr<DesktopShell> motion_shell_;
  CanvasPresentation cached_;
  std::string motion_location_,motion_network_,motion_application_;
};
}
