#include "diagnostics/function_probe.h"
#pragma once
#include "desktop/ui/screen_canvas.h"
#include "desktop/ui/render_frame.h"
#include "desktop/ui/paint_port.h"
#include "desktop/ui/render_hooks.h"
#include "desktop/ui/status_bar_cache.h"
#include "lunaros_text_raster.h"
#include "desktop/ui/system_view_painter.h"
#include "desktop/ui/system_views.h"
#include "desktop/application/catalog.h"
#include "desktop/ui/host_state.h"
#include "desktop/application/downloaded_view.h"
#include "desktop/ui/route.h"
#include <variant>
#include <string>
#include <type_traits>
namespace lunaredu::desktop {
template<class Bindings>
typename Bindings::Definition shell_ui_binding() {
  return {"system.shell",
    [](typename Bindings::CaptureContext &c) -> typename Bindings::Presentation {
      const auto &state=c.ui_state();
      return state.page==kUnavailableView ? ShellPresentation{state.unavailable_title,state.unavailable_reason} : ShellPresentation{};
    }, [](typename Bindings::InputContext &c) {
      if(c.primary)return false;
      if(c.ui_state().page==kUnavailableView && (c.back||c.confirm||
          c.command==int(DesktopCommand::Back)||c.command==int(DesktopCommand::Confirm)))c.show_shell();
      return true;
    }};
}
template<class Payload> struct UiRenderJob {
  UiFrame frame;
  Payload payload;
  RenderKind kind = RenderKind::Startup;
  bool full = false;
  int diagnostic = 0;
  uint32_t ticket = 0;
  void release_payload_leases() {
    // Semantic App payloads release immediately. Only bounded, non-secret
    // chrome text capacity remains owned by the three mailbox slots.
    payload.~Payload();::new(static_cast<void *>(&payload)) Payload();
    frame.release_payload_leases();kind=RenderKind::Startup;full=false;diagnostic=0;ticket=0;
  }
};
// A bounded mailbox contains exactly one active App snapshot per job. The
// product selects Payload alternatives; OS never names a concrete App.
template<class Payload> class UiRenderHost {
 public:
  using Job = UiRenderJob<Payload>;
  explicit UiRenderHost(uint64_t (*clock)(),
      typename display::ManagedScreen<Job>::RenderDispatch dispatch = {})
      : screen_(clock, dispatch), canvas_(screen_), clock_(clock) {}
  ~UiRenderHost() {
    // Reject destruction before canvas/Scene members borrowed by workers die.
    if(retained()) std::abort();
    std::string error; if(!stop(error)) std::abort();
  }
  bool setup(lunaros::KernelRuntime *runtime, display::ScreenBackend backend, const char *owner,
      display::ScreenPipelineTasks tasks, UiRenderHooks hooks, std::string &error,
      DesktopRenderOptions options = {},
      display::ScreenExecution execution = display::ScreenExecution::Workers,
      display::ScreenStorageBudget budget={4*1024*1024}) {
    if (screen_.lifecycle() != display::ScreenLifecycle::Unconfigured) { error="screen_already_configured"; return false; }
    if (!canvas_.service_bound()) { error="desktop_render_service_unbound"; return false; }
    if (backend.width != kDesktopCanvas.width || backend.height != kDesktopCanvas.height) { error="desktop_screen_dimensions_invalid"; return false; }
    if (!canvas_.configure(options)) { error="desktop_canvas_configuration_failed"; return false; }
    // Reserve the independently owned Scene raster/texture ceiling and known
    // worker stacks before admitting any frame storage. Shared LVGL/DMA remain
    // separately owned; products can include those in reserved_bytes.
    const uint64_t extra=uint64_t(kDesktopCanvas.bytes())+lunaros::graphics::RasterBudget{}.maximum_texture_bytes+
        lunaros::graphics::RasterBudget{}.maximum_canvas_cache_bytes+
        tasks.display.stack_bytes+tasks.renderer.stack_bytes;
    if(extra>budget.maximum_bytes || budget.reserved_bytes>budget.maximum_bytes-extra){error="screen_storage_budget_exceeded";return false;}
    budget.reserved_bytes+=size_t(extra);
    hooks_ = hooks;
    if (!screen_.observe_transfer(this, [](void *p, display::ScreenPatchView patch, display::ScreenRegion region) {
      auto &self = *static_cast<UiRenderHost *>(p);
      if (self.hooks_.transfer) self.hooks_.transfer(self.hooks_.context, patch, region, self.canvas_.frame_rotation(patch.pixels));
    })) { error="desktop_transfer_observer_unbound"; return false; }
    if(!screen_.observe_presented(this,[](void *p,display::ScreenFrameView frame,const display::ScreenRegion *regions,size_t count){
      auto &self=*static_cast<UiRenderHost *>(p);
      if(self.hooks_.presented)self.hooks_.presented(self.hooks_.context,frame,regions,count,self.canvas_.frame_rotation(frame.pixels));
    })){error="desktop_frame_observer_unbound";return false;}
    if (!screen_.observe_unchanged(this, [](void *p, display::FrameCanvas canvas) {
      auto &self = *static_cast<UiRenderHost *>(p);
      if (self.hooks_.unchanged) self.hooks_.unchanged(self.hooks_.context,canvas);
    })) { error="desktop_unchanged_observer_unbound"; return false; }
    return screen_.setup(runtime, backend, owner, tasks, [](void *p, Job &job) {
      static_cast<UiRenderHost *>(p)->execute(job);
    }, this, error, options.auxiliary_frames(), execution, budget);
  }
  template<class Capture> void publish(Capture capture) {
    screen_.publish(capture, [](Job &job) { job.full = true; });
  }
  void publish(RenderKind kind,const UiHostState &state,UiFramePresentation presentation,
      Payload &&payload,bool force,int diagnostic=0,uint32_t ticket=0) {
  lunaredu::system::probe::Scope diagnostic_scope(lunaredu::system::probe::Point::Publish);
    if(kind==RenderKind::Startup)graphics().select(UINT64_MAX,true);
    publish([&](Job &job){
      capture_ui_frame(job.frame,state,graphics().page(),std::move(presentation),kind);
      job.kind=kind;job.full=force||kind==RenderKind::Full;job.diagnostic=diagnostic;job.ticket=ticket;
      job.payload=std::move(payload);
    });
  }
  template<class Control> bool bind_control(Control &control,const char *output,std::string &error) {return screen_.bind_control(control,output,error);}
  bool stop(std::string &error) {if(!screen_.stop(error))return false;status_bar_cache_.reset();return true;}
  bool quiesce(std::string &error) {if(!screen_.quiesce(error))return false;status_bar_cache_.reset();return true;}
  void resume() {screen_.resume();}
  bool retained() const {return screen_.snapshot().storage.retained_bytes!=0;}
  bool ready() const { return screen_.ready(); }
  auto &graphics() { return canvas_.graphics(); }
  void wake() { screen_.wake(); }
  void request_redraw() { screen_.request_redraw(); }
  bool take_redraw() { return screen_.take_redraw(); }
  bool accepts_frame() { return screen_.accepts_frame(); }
  auto status() { return screen_.snapshot(); }
  bool service_render() { return screen_.service_render(); }
 private:
  class Port final : public UiPaintPort {
   public:
    Port(UiRenderHost &host, UiFrame &frame, uint32_t ticket) : host_(host), frame_(frame), ticket_(ticket) {}
    bool begin_region(uint16_t w,uint16_t h,uint16_t c,uint16_t y=0,bool clear=true) override {
      const bool ok=host_.canvas_.begin(w,h,c,y,clear);if(ok){decorated_=false;oriented_=false;failed_=false;}return ok;
    }
    bool flush_region(uint16_t x,uint16_t y) override { finish_content();if(failed_)return false;prepare();return host_.canvas_.flush(x,y); }
    display::FrameCanvas submission_canvas() const override { return host_.canvas_.submission_canvas(); }
    void damage_map(const display::FrameDamageMap &map) override {host_.canvas_.damage_map(map);}
    bool begin_canvas() override { return begin_region(kDesktopCanvas.width,kDesktopCanvas.height,lunaros::graphics::theme::background); }
    bool begin_widgets() override { return host_.canvas_.begin_widgets(lunaros::graphics::theme::appbackground); }
    auto graphics() -> lunaros::graphics::SceneRuntime & override { return host_.graphics(); }
    auto surface() -> lunaros::graphics::canvas::Surface override { return host_.canvas_.surface(); }
    RetainedSurface retained_surface(const void *owner,uint64_t key) override {
      const auto reuse=host_.canvas_.retained_surface(owner,key);
      return {host_.canvas_.chrome_surface(),reuse.valid,reuse.overwritten};
    }
    bool finish_widgets(const char *title,bool orient=true) override {
      if (!host_.canvas_.render_widgets()) return false;
      if(title)header(title);else decorated_=true;
      if (orient) rotate(rotation());
      return !failed_;
    }
    void header(const char *title) override {
      decorated_=true;
      const auto policy = resolve_render_policy(frame_.desktop.preferences.antialias,{},true);
      chrome(title,policy.antialias,frame_.detail.c_str());
    }
    void overlay(const char *title) override {
      decorated_=true;
      chrome(title,frame_.policy.antialias,nullptr);
    }
    void rotate(unsigned value) override {
      if(oriented_)return;
      if(owns_chrome()&&!decorated_)header(frame_.title.c_str());
      if(failed_)return;
      host_.canvas_.rotate(value);oriented_=true;
    }
    void present_widgets() override { finish_content();if(failed_)return;prepare();host_.canvas_.present_widgets(); }
    void flush() override { (void)flush_region(0,0); }
    unsigned rotation() const override { return frame_.rotation(); }
    const DesktopShell &shell() const override { return frame_.desktop; }
   private:
    void chrome(const char *title,bool aa,const char *detail){
      system::probe::Scope diagnostic_chrome(system::probe::Point::UiChrome);
      const bool full=frame_.system||status_overlay_shows_bar(frame_.attention,frame_.desktop.status_navigation,frame_.desktop.preferences.auto_hide_status);
      const lunaros::graphics::CanvasSpec target{host_.canvas_.width(),host_.canvas_.height(),kDesktopCanvas.stride};
      auto &renderer=host_.graphics().scene();
      bool ok=false;
      if(full&&host_.canvas_.width()==kDesktopCanvas.width&&host_.canvas_.height()>=status_layout::height)
        ok=host_.status_bar_cache_.paint(renderer,target,title,frame_.attention,host_.canvas_.chrome_pixels(),aa,detail,lunaros::text_raster::font_generation());
      else if(!full&&host_.canvas_.width()==kDesktopCanvas.width)
        ok=host_.status_bar_cache_.paint_hidden(renderer,target,host_.canvas_.chrome_pixels(),aa);
      else ok=paint_status_overlay(renderer,target,host_.canvas_.chrome_pixels(),title,frame_.attention,
          frame_.desktop.status_navigation,full?false:frame_.desktop.preferences.auto_hide_status,aa,detail);
      if(!ok){failed_=true;host_.canvas_.reject_frame();}
    }
    bool owns_chrome()const{return frame_.overlays&&frame_.page!=kShellView&&
      host_.canvas_.width()==kDesktopCanvas.width&&host_.canvas_.height()==kDesktopCanvas.height;}
    void finish_content(){if(owns_chrome()&&!oriented_)rotate(rotation());}
    void prepare(){if(host_.hooks_.prepared)host_.hooks_.prepared(host_.hooks_.context,host_.canvas_.submission_canvas(),ticket_);}
    UiRenderHost &host_; UiFrame &frame_;
    uint32_t ticket_=0;
    bool decorated_=false,oriented_=false,failed_=false;
  };
  void execute(Job &job) {
    system::probe::Scope diagnostic_execute(system::probe::Point::UiExecute);
    auto &frame=job.frame; frame.rebind();
    const bool admitted=!hooks_.before || hooks_.before(hooks_.context,job.ticket,frame.page);
    if (admitted) render(job);
    if (hooks_.after) hooks_.after(hooks_.context,job.ticket);
    canvas_.end_frame();
    frame.desktop.surface_state.reset();
  }
  template<class RenderPayload>
  static auto payload_cache(const RenderPayload &payload, int) -> decltype(payload.render_cache()) {
    return payload.render_cache();
  }
  template<class RenderPayload>
  static UiRenderCache payload_cache(const RenderPayload &, long) { return {}; }
  void render(Job &job) {
    auto &frame=job.frame;
    UiRenderCache next;
    if(job.kind!=RenderKind::Startup&&job.kind!=RenderKind::Diagnostic)
      next=std::visit([](const auto &payload)->UiRenderCache {
        return payload_cache(payload, 0);
      },job.payload);
    if(cache_.context!=next.context&&cache_.inactive)cache_.inactive(cache_.context);
    cache_=next;
    if (job.kind==RenderKind::Startup || job.kind==RenderKind::Diagnostic) {
      frame.desktop.home_navigation.reset(false); frame.desktop.edge_navigation.reset();
    }
    if(job.kind!=RenderKind::Startup){
      if(frame.page==kShellView&&frame.desktop.surface==DesktopSurface::Launcher)graphics().prepare_canvas(frame.ui_page);
      else if(!frame.widgets)graphics().prepare_canvas(frame.ui_page);
    }
    // Consume cross-owner full-redraw requests before choosing this job's
    // incremental policy; a capture invalidation must not lose the full paint.
    screen_.begin_render();const bool full=job.full || screen_.recovery_pending();
    canvas_.configure({&frame.desktop,frame.attention,frame.policy,frame.ui_page,frame.rotation(),
        ((frame.ui_page.widgets && frame.widgets)||frame.incremental)&&!frame.system,
        // Coalescing may require full paint, while transport can still compare
        // final pixels with its acknowledged baseline. Recovery forces I/O too.
        frame.overlays,screen_.recovery_pending(),frame.incremental?std::string_view(frame.application):std::string_view{},
        (uint64_t(frame.page.key)<<32)^frame.content_revision,
        frame.desktop.preferences.animations&&job.kind!=RenderKind::Startup&&job.kind!=RenderKind::Diagnostic&&!frame.system,
        frame.page==kShellView&&frame.desktop.surface==DesktopSurface::Launcher});
    if(canvas_.defer_motion_repaint())return;
    Port port(*this,frame,job.ticket);
    if (job.kind==RenderKind::Navigation && frame.desktop.home_navigation.progress>0 &&
        canvas_.navigation().captured(frame.desktop.home_navigation.epoch) && !screen_.recovery_pending()) {
      if (port.begin_canvas()) port.flush();
      return;
    }
    if (job.kind==RenderKind::Startup) { paint_startup(frame.device_name.c_str(),port,frame.startup_elapsed_ms); return; }
    if (job.kind==RenderKind::Diagnostic) { paint_diagnostic(job.diagnostic!=0,port); return; }
    if (full) screen_.consume_recovery();
    std::visit([&](const auto &payload) {
      using T = std::decay_t<decltype(payload)>;
      if constexpr (std::is_same_v<T,ShellPresentation>) {
        if (!payload.unavailable_title.empty()) payload.paint(port,full);
        // Launcher wallpaper paints every pixel. Clearing the same PSRAM frame
        // immediately beforehand adds a redundant full-screen memory pass.
        else if(frame.desktop.surface==DesktopSurface::Launcher){
          if(port.begin_region(kDesktopCanvas.width,kDesktopCanvas.height,0,0,false)){
            if(screen_.timed(display::ScreenPipelinePhase::Raster,[&]{return draw_launcher(frame.desktop,frame.status,canvas_.pixels(),screen_text_painter(),false,nullptr,&graphics().scene());}))port.flush();
            else {screen_.invalidate_frame();screen_.request_redraw();}
          }
        }else if(canvas_.begin_widget_target(0)){
          if(screen_.timed(display::ScreenPipelinePhase::Raster,[&]{return lunaros::graphics::render_desktop(graphics(),frame.ui_page,frame.desktop,frame.status,canvas_.widget_output(),screen_text_painter());})){
            canvas_.widgets_rendered();port.present_widgets();
          }
        }
      } else payload.paint(port,full);
    },job.payload);
  }
  display::ManagedScreen<Job> screen_;
  ScreenCanvas<Job> canvas_;
  uint64_t (*clock_)();
  UiRenderHooks hooks_;
  UiRenderCache cache_;
  StatusBarRasterCache status_bar_cache_;
};
}
