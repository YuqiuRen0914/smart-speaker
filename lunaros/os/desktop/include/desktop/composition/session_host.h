#include "diagnostics/function_probe.h"
#include "app/lifecycle/app_controller_view.h"
#pragma once
#include "desktop/composition/selection.h"
#include "desktop/ui/bindings.h"
#include "desktop/ui/status.h"
#include "display/pipeline/frame_cadence.h"
#include "desktop/ui/model_observation.h"
#include "desktop/ui/input_timing.h"
#include <cstring>
#include <utility>

namespace lunaredu::lunaros {
// Default displayed composition. App descriptions select their own sessions;
// Platform supplies hardware facts and Extension owns native-App decisions. This host
// introduces no task, allocation scheme, physical mapping or business owner.
template<class Platform, class Extension> class DesktopSessionHost {
 public:
  using Configuration=Platform;
  using Selection=std::decay_t<decltype(Platform::selection())>;
  using Sessions=typename Selection::template Sessions<DesktopSessionHost>;
  using Model=typename Platform::Model;
  using State=typename Platform::State;
  using Payload=typename Platform::Payload;
  using Command=typename Platform::Command;
  using Render=typename Platform::Render;
  using Paint=typename Extension::Paint;
  DesktopSessionHost():state_(extension_.state()),ui_port_(*this),
      sessions_(*this),bindings_(ui_port_) {}
  DesktopSessionHost(const DesktopSessionHost &)=delete;
  DesktopSessionHost &operator=(const DesktopSessionHost &)=delete;
  State &state(){return state_;}
  Extension &extension(){return extension_;}
  auto &applications(){return applications_;}
  auto &application_bindings(){return bindings_;}
  desktop::DesktopUiHost &ui(){return ui_port_;}
  template<class Entry,class Action> bool with_app(Action action){return sessions_.template with<typename Entry::template Session<DesktopSessionHost>>(action);}
  bool temporary_view(app::ViewRoute page) const{return sessions_.temporary_view(page);}
  void suspend_application_view(){sessions_.suspend_view();}
  void release_transient_frames(){sessions_.release_transient_frames();}
  auto render_policy(bool system){return Render::drawing_policy(state_,system);}
  std::string running_application() const{return sessions_.running_application();}
  bool take_redraw(){return Render::take_redraw();}
  // A foreground producer can publish between input/model updates. Forward its
  // retained redraw without re-running input hooks; the renderer owns pacing.
  bool service_redraw() {
    return ui_port_.service_redraw();
  }
  auto notification(const char *owner,const char *key,const char *title,bool active,uint64_t now){return Platform::notification(owner,key,title,active,now);}
  const Model &model() const{return active_model_?*active_model_:state_.last_model;}
  // Products sealing capabilities during core composition may declare their
  // selected foregrounds before display/services are ready. This enters no App.
  bool declare_foregrounds(ApplicationContext *device,typename Extension::Lifecycle lifecycle,std::string &error) {
    if(foregrounds_declared_){error="desktop_foregrounds_already_declared";return false;}
    // Sessions may also own foreground declarations (for example a retained
    // game host). Declare every selected owner before the product seals the
    // registry; hardware/frame preparation remains in start().
    foregrounds_declared_=declare_applications(device,lifecycle,error) &&
        sessions_.declare(*device,error);
    return foregrounds_declared_;
  }
  bool start(KernelRuntime *runtime,ApplicationContext *device,typename Extension::Lifecycle lifecycle,
      const char *&stage,std::string &error) {
    if(!start_desktop_session(ui_port_,device,[&](std::string &e){
      return prepare_desktop_applications(ui_port_,Platform::catalog(),runtime,device,sessions_,observers_,
          {Platform::shell_id,Platform::version(),ui_bindings().valid(),Render::ready(),Platform::selection().providers(),Platform::selection().provider_count()},e);
    },[&](std::string &e){
      return foregrounds_declared_||declare_foregrounds(device,lifecycle,e);
    },stage,error))return false;
    starting_=state_.desktop.preferences.animations;
    startup_started_ms_=Platform::now_ms();startup_last_frame_ms_=0;
    state_.startup_elapsed_ms=0;
    publish(starting_?desktop::RenderKind::Startup:desktop::RenderKind::Full);return true;
  }
  auto pipeline_status(){return Render::status();}
  void force_redraw(){Render::request_redraw();}
  bool desktop_command(desktop::DesktopCommand command){return ui_port_.queue_command(command);}
  bool diagnostic_pattern(const char *pattern) {
    if(!state_.ready||!pattern||(std::strcmp(pattern,"bars")&&std::strcmp(pattern,"orientation")))return false;
    publish(desktop::RenderKind::Diagnostic,nullptr,std::strcmp(pattern,"orientation")==0);
    state_.force_redraw=true;return true;
  }
  bool native_active(){return extension_.active(*this);}
  bool control_active(){return extension_.control_active(*this);}
  bool consumes_controls(){return extension_.consumes_controls(*this);}
  auto capabilities(){return extension_.capabilities(*this);}
  template<class Workflow> void report_selection(Workflow workflow,bool saved) {
    if(extension_.view().report_selection(workflow,state_.page,saved))publish(desktop::RenderKind::Full,&state_.last_model);
  }
  auto status(){return extension_.status(*this);}
  // Caller-owned storage can reuse its strings on the UI owner. As with status,
  // borrowed shell IDs must be copied before publication across executors.
  template<class Status> void status_into(Status &result){extension_.status_into(*this,result);}
  // Narrow observations on the owning UI executor avoid constructing the
  // owned strings and diagnostic payloads needed by a complete snapshot.
  bool ready() const{return state_.ready;}
  bool foreground_is(const char *id) const {
    const auto *owner=applications_.controller();
    const auto *current=owner?lunaredu::app::AppControllerView{owner}.active_application_id():nullptr;
    return id&&id[0]&&current&&!std::strcmp(current,id);
  }
  // These owners expose locked/atomic counters. Capture on an actual
  // diagnostic read, without allocating or refreshing a resident UI cache.
  desktop::UiPerformance performance_snapshot() const {
    return {Render::status(),Render::graphics().input_stats(),Render::graphics().scene().stats()};
  }
  auto diagnostic_status(){
    auto value=status();
    value.performance=std::make_shared<const desktop::UiPerformance>(performance_snapshot());
    return value;
  }
  void update(const Model &sample,int64_t now) {
    if(!state_.ready)return;
    ModelScope scope(*this,sample);
    if(starting_){
      const int32_t delta=static_cast<int32_t>(uint32_t(now/1000)-startup_started_ms_);
      // A caller may have sampled before start() completed. Keep startup
      // active until a fresh tick instead of treating underflow as completion.
      if(delta<0)return;
      const uint32_t elapsed=static_cast<uint32_t>(delta);
      if(elapsed<desktop::system_canvas::startup_duration_ms && !blocked()){
        if(elapsed*display::kScreenTargetFps/1000>startup_last_frame_ms_*display::kScreenTargetFps/1000){
          state_.startup_elapsed_ms=elapsed;startup_last_frame_ms_=elapsed;
          publish(desktop::RenderKind::Startup,&sample);
        }
        return;
      }
      starting_=false;state_.force_redraw=true;ui_port_.session().rearm(state_.desktop,Render::graphics());
    }
    ui_port_.update(Platform::input_sample(sample,state_,now));
  }
  Command take_action(){const auto action=state_.pending_action;state_.pending_action=Extension::no_action;return action;}
  void queue_action(Command action){state_.pending_action=action;}
  void rearm() {
    if(!state_.ready)return;
    extension_.view().rearm();state_.pending_action=Extension::no_action;
    ui_port_.session().rearm(state_.desktop,Render::graphics());
    ui_port_.coordinate_foreground([&]{extension_.rearm_destination(*this);});
  }
  bool application_entered(app::ViewRoute page,const Model &sample) {
    ModelScope scope(*this,sample);
    return state_.page==page || ui_port_.application_entered({page});
  }
  void change_page(app::ViewRoute page,const Model &sample){ModelScope scope(*this,sample);(void)ui_port_.change_page(page);}
  void publish(desktop::RenderKind kind,const Model *sample=nullptr,int diagnostic=0,uint32_t ticket=0) {
    if(!Render::ready())return;
    desktop::InputWorkScope capture_work(desktop::InputWorkTimingStage::Snapshot);
    if(kind==desktop::RenderKind::Startup)Render::graphics().select(UINT64_MAX,true);
    else ui_port_.select_widgets();
    const auto &captured=sample?*sample:state_.last_model;
    auto payload=capture(captured,Extension::full_paint,-1,ticket);
    Render::publish(kind,state_,captured,state_.force_redraw,std::move(payload),diagnostic,ticket);
  }
  void publish_application(const Model &sample,Paint request,int cells=-1) {
    if(!Render::ready())return;
    desktop::InputWorkScope capture_work(desktop::InputWorkTimingStage::Snapshot);
    ui_port_.select_widgets();auto payload=capture(sample,request,cells,0);
    Render::publish(desktop::RenderKind::Application,state_,sample,state_.force_redraw,std::move(payload));
  }
  bool header_changed(const Model &a,const Model &b) const {
    return extension_.presentation_changed(a,b) ||
        Platform::observe_ui_model(a).header_changed(Platform::observe_ui_model(b));
  }
  bool system_owned() const{return Platform::observe_ui_model(model()).system_owned;}
  bool blocked() const{return system_owned()||extension_.blocked(model());}
 private:
  struct InputBase {
    DesktopSessionHost &host;const Model &model;int64_t now_us;
    bool a,b,confirm,back;int navigation,encoder,command;bool top_owned,edge_back,primary;
    desktop::TouchEvent pointer{};bool pointer_tracking=false;
    desktop::UiHostState &ui_state(){return host.state_;}
    void show_shell(){host.change_page(desktop::kShellView,model);}
    bool system_owned() const{return Platform::observe_ui_model(model).system_owned;}
  };
  using InputContext=typename Extension::template InputContext<typename Sessions::template Context<InputBase>>;
  struct CaptureBase {
    DesktopSessionHost &host;const Model &model;Paint request;int cells;uint32_t ticket;
    const desktop::UiHostState &ui_state() const{return host.state_;}
    app::ViewRoute route() const{return host.state_.page;}
  };
  using CaptureContext=typename Extension::template CaptureContext<typename Sessions::template Context<CaptureBase>>;
  struct EntryBase {
    DesktopSessionHost &host;const Model &model;
    bool enter_ui(desktop::UiEntry entry) {
      ModelScope scope(host,model);return host.ui_port_.enter_ui(entry);
    }
  };
  using EntryContext=typename Extension::template EntryContext<EntryBase>;
  using LaunchContext=typename Extension::template LaunchContext<typename Sessions::template Context<EntryContext>>;
  using UiBindings=app::UiBindings<CaptureContext,InputContext,EntryContext,LaunchContext,Payload>;
  static const UiBindings &ui_bindings(){static const auto bindings=Platform::template bindings<UiBindings>();return bindings;}
  Payload capture(const Model &sample,Paint request,int cells,uint32_t ticket) {
  lunaredu::system::probe::Scope diagnostic_scope(lunaredu::system::probe::Point::UiCapture);
    CaptureContext context{sessions_.context(CaptureBase{*this,sample,request,cells,ticket})};
    return ui_bindings().capture(extension_.capture_route(state_.page,sample),context);
  }
  bool declare_applications(ApplicationContext *device,typename Extension::Lifecycle lifecycle,std::string &error) {
    extension_.configure(lifecycle);
    extension_.control_profile(Platform::control_profile());
    extension_.initialize_model(state_.last_model);
    const app::HostedApplicationPorts hosted{this,
      [](void *p,const char *id,std::string &e){auto &h=*static_cast<DesktopSessionHost *>(p);return h.extension_.prepare(id,e);},
      [](void *p,const char *id,std::string &e){auto &h=*static_cast<DesktopSessionHost *>(p);return h.extension_.backend_entered(id,e);},
      [](void *p,const char *id,std::string &e){auto &h=*static_cast<DesktopSessionHost *>(p);return h.extension_.backend_leaving(id,e);}};
    typename Platform::ApplicationPorts ports;
    static_cast<DesktopApplicationPorts &>(ports)=bindings_.ports(hosted,Platform::now_ms);
    sessions_.application_ports(ports);
    extension_.application_ports(ports,*this);
    if(!device||!device->application()){error="desktop_application_owner_missing";return false;}
    if(!applications_.declare(*device->application(),ports,error))return false;
    ui_port_.bind_applications(*device->application());
    return true;
  }
 public:
  void record_input(const Model &sample) {
  lunaredu::system::probe::Scope diagnostic_scope(lunaredu::system::probe::Point::ModelCopy);
    desktop::observe_model(state_.last_model,sample);state_.have_model=true;
    const auto observed=Platform::observe_ui_model(sample);
    state_.last_buttons=observed.buttons;state_.last_encoder_position=observed.encoder_position;
  }
 private:
  class UiPort final : public desktop::DesktopUiHost {
   public:
    explicit UiPort(DesktopSessionHost &host):DesktopUiHost(host.state_,Platform::ui_options(),&host.observers_),host_(host){}
    graphics::SceneRuntime &ui_graphics() override{return Render::graphics();}
    void log_ui(const char *,const std::string &message,desktop::UiLogLevel level) override{Platform::log(message,level);}
    void wake_ui() override{Render::wake();}
    void render_ui(uint32_t ticket=0) override{host_.publish(desktop::RenderKind::Full,&host_.model(),0,ticket);}
    void rearm_ui() override{host_.rearm();}
    bool ui_blocked() const override{return host_.blocked();}
    display::ScreenSessionSnapshot ui_metrics() override{return Render::status();}
   private:
    desktop::DesktopUiPolicy policy() const override {
      desktop::InputWorkScope scope(desktop::InputWorkTimingStage::Policy);
      auto result=host_.extension_.policy(host_,host_.model());result.application_widgets=host_.sessions_.widgets();return result;
    }
    void tick_applications(int64_t) override {
      desktop::InputWorkScope hooks_work(desktop::InputWorkTimingStage::SessionHooks);
      host_.sessions_.tick(!host_.blocked());
    }
    bool safety() override{return host_.extension_.safety(host_,host_.model());}
    void observe_release(bool released) override{host_.extension_.observe_release(released,host_.model());}
    void prepare_destination() override {
      if(const auto destination=host_.sessions_.destination())host_.change_page(*destination,host_.model());
      else host_.extension_.prepare_destination(host_,host_.model());
    }
    bool application_input(desktop::UiInputEvents e,int64_t now) override {
      return ui_bindings().input_with(host_.state_.page,input_cursor_,[&]{
        const auto &m=host_.model();const auto keys=Platform::keys(m);
        return InputContext{host_.sessions_.context(InputBase{host_,m,now,keys.first,keys.second,e.confirm,e.back,e.navigation,e.encoder,e.command,e.top_owned,e.edge_back,e.primary,e.pointer,e.pointer_tracking})};
      });
    }
    void shell_action(const char *id) override {
      if(!id)return;
      auto &s=host_.state_;s.navigation.menu=s.desktop.active_menu?s.desktop.active_menu->id:nullptr;s.navigation.item=id;
      if(!host_.extension_.shell_action(host_,id,host_.model()))(void)host_.sessions_.shell_action(id);
    }
    bool launcher_combo_shortcut() override{return host_.extension_.launcher_back(host_,host_.model());}
    bool back_application() override{return host_.extension_.back_application(host_,host_.model());}
    void home_admitted() override{host_.extension_.exited(host_);}
    bool shell_changed() override {
      desktop::InputWorkScope scope(desktop::InputWorkTimingStage::ShellObserve);
      const auto &m=host_.model();const auto &last=host_.state_.last_model;
      return host_.extension_.observe_menu(host_,m,now_us())||!host_.state_.have_model||host_.extension_.presentation_changed(m,last)||
          Platform::observe_ui_model(m).shell_changed(Platform::observe_ui_model(last));
    }
    void sample_observed() override{desktop::InputWorkScope scope(desktop::InputWorkTimingStage::ModelObservation);desktop::observe_model(host_.state_.last_model,host_.model());host_.state_.have_model=true;}
    void cancel_application_input() override{host_.sessions_.cancel_input();}
    void rearm_application() override{host_.extension_.view().rearm();}
    app::ViewRoute qualify_destination(app::ViewRoute page) override{return host_.extension_.view().qualify_destination(page);}
    void destination_committed(app::ViewRoute page) override {
      host_.sessions_.destination_committed(page);
      host_.extension_.view().destination_committed(page);
    }
    void foreground_entered(const char *id) override {
      EntryContext context{EntryBase{host_,host_.state_.last_model}};ui_bindings().entered(id,context);
    }
    desktop::ApplicationLaunchResult launch_application(const char *id,std::string &error) override {
      ModelScope scope(host_,host_.model());const auto *binding=ui_bindings().find(id);LaunchContext context{host_.sessions_.context(EntryContext{EntryBase{host_,host_.model()}})};
      if(!binding||!binding->launch)return desktop::ApplicationLaunchResult::Unknown;
      return binding->launch(context,error)?desktop::ApplicationLaunchResult::Started:desktop::ApplicationLaunchResult::Rejected;
    }
    bool launch_downloaded(const char *id) override {
      ModelScope scope(host_,host_.model());return host_.sessions_.launch_downloaded(id);
    }
    bool widget_action(const graphics::Action &action,bool &confirm,bool &back) override {
      if(host_.extension_.owns(host_.state_.page))return host_.extension_.view().widget_action(host_.state_.page,action.id,confirm,back);
      return DesktopUiHost::widget_action(action,confirm,back);
    }
    void paint_navigation() override{host_.publish(desktop::RenderKind::Navigation,&host_.model());}
    void request_redraw() override{Render::request_redraw();}
    bool take_redraw() override{return Render::take_redraw();}
    void pointer_ended(const desktop::TouchEvent &touch) override{Platform::observe_touch(host_,host_.model(),touch,now_us());}
    typename UiBindings::InputCursor input_cursor_;
    DesktopSessionHost &host_;
  };
  struct ModelScope {
    DesktopSessionHost &host;const Model *previous;
    ModelScope(DesktopSessionHost &h,const Model &sample):host(h),previous(h.active_model_){h.active_model_=&sample;}
    ~ModelScope(){host.active_model_=previous;}
  };
  Extension extension_;
  State state_;
  DesktopShellObservers observers_;
  UiPort ui_port_;
  Sessions sessions_;
  typename Platform::Applications applications_;
  DesktopApplicationBindings bindings_;
  const Model *active_model_=nullptr;
  bool starting_=false;
  bool foregrounds_declared_=false;
  uint32_t startup_started_ms_=0,startup_last_frame_ms_=0;
};
}
