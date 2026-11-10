#include "ui/entry_internal.h"
#include <cstdio>
#include "desktop/application/downloaded_ui_snapshot.h"
#include "navigation/view_navigation_internal.h"
#include "desktop/ui/host.h"
#include "desktop/ui/render_frame.h"
#include "shell/maintenance.h"
#include "ui/host_interaction.h"
#include "app/lifecycle/app_controller.h"
#include <cmath>
#include "desktop/ui/input_timing.h"
#include "diagnostics/elapsed_work_report.h"
#ifdef ESP_PLATFORM
#include "diagnostics/cpu_monitor.h"
#include <esp_attr.h>
#include <esp_timer.h>
#endif

namespace lunaredu::desktop {
void DesktopUiHost::restore_preferences(bool preserve_missing_startup) {
    auto preferences=load_desktop_preferences(options_.default_startup);
    if(options_.rendering.default_grid&&!preferences.customized)preferences.layout=DesktopLayout::Grid;
    state_.desktop.grid_columns=std::clamp<uint8_t>(options_.rendering.grid_columns,1,3);
    state_.desktop.grid_rows=std::clamp<uint8_t>(options_.rendering.grid_rows,1,3);
    state_.desktop.set_preferences(preferences,preserve_missing_startup);
    if(lunaros::graphics::metrics::native&&options_.rendering.phone_desktop){
      HomeLayout saved;bool found=false;const bool read=load_home_layout(saved,found);
      state_.desktop.home.configure(state_.desktop,options_.home_widgets,options_.home_widget_count,read&&found?&saved:nullptr,!read,!preserve_missing_startup);
    }
    if constexpr(!lunaros::graphics::metrics::native){
      CompactLayout saved;bool found=false;const bool read=load_compact_layout(saved,found);
      state_.desktop.compact.configure(state_.desktop,read&&found?&saved:nullptr,!read,!preserve_missing_startup);
    }
    if(state_.desktop.restore_startup_selection(options_.defer_startup_application))state_.navigation.launcher_index=int(state_.desktop.focus);
  }

bool DesktopUiHost::queue_command(DesktopCommand value) {
    if(static_cast<unsigned>(value)>static_cast<unsigned>(DesktopCommand::Home))return false;
    int empty=-1;return commands_.compare_exchange_strong(empty,int(value));
  }

bool DesktopUiHost::take_command(DesktopCommand &value) {
    const int pending=commands_.exchange(-1);
    if(pending<0)return false;
    value=static_cast<DesktopCommand>(pending);return true;
  }

void DesktopUiHost::select_widgets() {
    InputWorkScope scope(InputWorkTimingStage::WidgetSelection);
    session_.select_widgets(state_.desktop,ui_graphics(),unsigned(state_.page),state_.page==kShellView,state_.page.widgets);
  }

const char *DesktopUiHost::foreground(app::ViewRoute page) const {
    if(page==kUnavailableView)return options_.shell_id;
    return page==kShellView?desktop_foreground(state_.desktop,options_.shell_id,options_.settings_id,
        options_.menus,options_.menu_count):page.owner;
  }

bool DesktopUiHost::change_page(app::ViewRoute next) {
    return commit_page(next,true);
  }

bool DesktopUiHost::commit_page(app::ViewRoute next,bool restore_previous_shell) {
    policy_valid_=false;
    next=qualify_destination(next);
    if(!commit_view_transition(next,state_.page,foreground(next),[&](const char *id){
      if(transitioning_||!applications_)return true;
      std::string error;Transition transition(*this);
      if(applications_->switch_to(id,error))return true;
      log_ui("desktop","Foreground transition rejected: "+error,UiLogLevel::Warning);return false;
    },ui_graphics(),state_.desktop,state_.touch_router,state_.navigation,state_.touch_available,restore_previous_shell))return false;
    destination_committed(next);++state_.input_epoch;state_.stick_navigation.reset();state_.stick_merger.reset();state_.desktop.reset_joystick();state_.force_redraw=false;
    select_widgets();render_ui();sample_observed();return true;
  }

bool DesktopUiHost::enter_ui(UiEntry entry)  {
    // Policy may admit a guarded destination instead of the requested route.
    // Report the checked transition result so lifecycle cleanup can complete.
    // An explicit menu/surface/tile is already the requested Shell destination.
    // Restoring an old return location here can replace it with Home and make
    // the next input tick undo a successfully admitted foreground switch.
    return commit_page(apply_ui_entry(state_.desktop,entry),entry.kind==UiEntryKind::View);
  }

bool DesktopUiHost::launch_ui(const char *id,bool downloaded)  {
    if(!id||!*id||ui_blocked())return false;
    std::string error;
    const auto result=host_detail::checked_application_launch(state_.desktop,state_.page,
        [&](std::string &failure){return launch_application(id,failure);},[&]{ui_graphics().invalidate();request_redraw();},error);
    if(result!=ApplicationLaunchResult::Unknown) {
      if(result==ApplicationLaunchResult::Rejected)log_ui("desktop",std::string("Application ")+id+" launch rejected: "+error,UiLogLevel::Warning);
      return result==ApplicationLaunchResult::Started;
    }
    if(downloaded||state_.desktop.find_app(id)>=options_.builtin_count)return launch_downloaded(id);
    if(!state_.desktop.activate(id))return false;
    state_.navigation.launcher_index=int(state_.desktop.focus);
    return change_page(kShellView);
  }

void DesktopUiHost::update(DesktopUiSample sample) {
    input_pass_=true;policy_valid_=false;
    struct EndPass {bool &active;~EndPass(){active=false;}} end_pass{input_pass_};
    if(!sample.input.axes_fresh||sample.input.blocked){
      state_.stick_navigation.reset();state_.stick_merger.reset();state_.desktop.reset_joystick();
      sample.input.horizontal=sample.vertical=0;
    }else {
      const auto axis=state_.stick_merger.update({sample.input.horizontal,sample.vertical},
          {sample.input.secondary_horizontal,sample.input.secondary_vertical});
      sample.input.horizontal=axis.x;sample.vertical=axis.y;
    }
    sample.input.vertical=sample.vertical;
    sample.input.controls.defer_desktop_buttons=state_.page==kShellView&&state_.desktop.surface==DesktopSurface::Launcher;
    sample_=sample;
    UiInputHost input(session_,state_,state_.desktop,ui_graphics());input.update(sample.input,*this);
  }

bool DesktopUiHost::service_redraw() {
    InputWorkScope scope(InputWorkTimingStage::Redraw);
    if(!state_.ready||!state_.have_model||!take_redraw())return false;
    render_ui();return true;
  }

void DesktopUiHost::begin_tick()  {
    if(home_uninstall_){
      std::string error;
      if(home_uninstall_->poll(state_.desktop,home_maintenance_,error)){
        if(!error.empty()){
          auto &shell=state_.desktop;
          if(shell.home.enabled)shell.home.error="卸载未完成，请检查后重试";
          else shell.compact.error="卸载未完成，请检查后重试";
          log_ui("desktop","Application uninstall outcome: "+error,UiLogLevel::Warning);
        }
        home_uninstall_.reset();state_.force_redraw=true;
      }
    }
    tick_applications(sample_.input.now_us);
    if(ui_blocked()||state_.page!=kShellView)suspend_observers(uint32_t(sample_.input.now_us/1000));
  }

NavigationResult DesktopUiHost::navigate()  {
    const auto current_policy=input_policy();
    NavigationInput input;input.touch=sample_.input.touch;input.now_ms=uint32_t(sample_.input.now_us/1000);input.blocked=sample_.input.blocked;
    input.home_enabled=input.touch.available&&!input.blocked&&(state_.page!=kShellView||state_.desktop.surface!=DesktopSurface::Launcher);
    input.edge_enabled=input.home_enabled&&(state_.page!=kShellView||!state_.desktop.paged_menu());
    input.rotate_touch=!state_.page.fixed_orientation&&!current_policy.orientation_locked&&state_.desktop.preferences.rotation;
    input.physical_activity=sample_.input.controls.buttons!=state_.last_buttons||sample_.input.controls.encoder_position!=state_.last_encoder_position;
    input.edge_interrupted=input.physical_activity||commands_.load()>=0||
        (std::abs(sample_.input.horizontal)>=420||std::abs(sample_.vertical)>=420);
    input.attention=current_policy.attention;input.low_battery=current_policy.low_battery;
    input.home_card_animation=options_.rendering.home_card_animation;
    const auto result=navigation_.update(input);
    if(result.consumed){sample_observed();session_.observe(sample_.input.controls.buttons,sample_.input.controls.encoder_position);}
    return result;
  }

int DesktopUiHost::command(bool system)  {
    policy_valid_=false; // Destination/release hooks may change policy.
    DesktopCommand queued{};int result=take_command(queued)?int(queued):-1;const bool allowed=input_policy().navigation_allowed;
    if(system&&allowed)result=int(DesktopCommand::System);
    if(allowed&&(result==int(DesktopCommand::Home)||result==int(DesktopCommand::System)))change_page(kShellView);
    return result;
  }

void DesktopUiHost::widgets(bool &confirm,bool &back)  {
    InputWorkScope scope(InputWorkTimingStage::Widgets);
    // Shell contact has already generated actions for the selected page.
    // Drain them before selecting a new generation for its resulting surface.
    if(!shell_widgets_)select_widgets();
    else if(sample_.input.touch.pressed||shell_touch_down_)wake_ui();
    if(input_policy().application_widgets)return;
    lunaros::graphics::Action a;
    while(ui_graphics().take_action(a)) {
      policy_valid_=false; // Actions may change route, resources or application state.
      if(!state_.page.widgets)continue;
      state_.force_redraw|=state_.page==kShellView?state_.desktop.apply_widget_action(a):widget_action(a,confirm,back);
    }
  }

bool DesktopUiHost::shell_input(UiInputEvents e,bool touch_changed)  {
    if(state_.page!=kShellView||sample_.input.blocked)return false;
    if(e.combo&&state_.desktop.surface==DesktopSurface::Launcher){
      const bool launched=launcher_combo_shortcut();
      sample_observed();session_.observe(sample_.input.controls.buttons,sample_.input.controls.encoder_position);return launched;
    }
    auto &shell=state_.desktop;const auto now=uint32_t(sample_.input.now_us/1000);
    bool changed=shell_changed()||sample_.changed||state_.force_redraw||touch_changed;
    // Validate the current contact before advancing hold/drag timers. A slow
    // frame may leave an old pressed sample behind; its stale release must
    // cancel that contact, rather than open Edit as a fabricated long press.
    shell.now_ms=now;shell.clock_ready=true;
    shell.touch_enabled=sample_.input.touch.available;shell.keys_enabled=true;
    {
      struct WidgetPass {
        bool &flag;bool previous;
        explicit WidgetPass(bool &value):flag(value),previous(value){flag=true;}
        ~WidgetPass(){flag=previous;}
      } pass(shell_widgets_);
      changed=host_detail::update_shell_interaction(shell,ui_graphics(),{state_.stick_navigation,state_.force_redraw,state_.navigation.launcher_index},
          {sample_.input.touch,now,e.top_owned,sample_.input.blocked,sample_.system_pressed,e.confirm,e.back,e.encoder,
           sample_.input.horizontal,sample_.vertical,e.command,state_.page.key,sample_.input.axes_fresh,
           sample_.input.touch.pressed||shell_touch_down_||shell.contact||shell.blocked_contact||
               shell.home.contact||shell.compact.contact},*this,changed);
      // Retain one release/cancellation pass; a steady released source has no
      // contact work. Commands, motion, observers and persistence still run.
      shell_touch_down_=sample_.input.touch.pressed;
    }
    changed|=shell.tick(now);
    changed|=state_.force_redraw;state_.force_redraw=false;if(state_.page==kShellView&&changed)render_ui();
    sample_observed();session_.observe(sample_.input.controls.buttons,sample_.input.controls.encoder_position);return true;
  }

bool DesktopUiHost::leave_for_home(std::string &error)  {
    if(!applications_){error="ui_application_owner_missing";return false;}
    Transition transition(*this);if(!applications_->switch_to(options_.shell_id,error))return false;
    home_admitted();return true;
  }

bool DesktopUiHost::synchronize()  {
    InputWorkScope scope(InputWorkTimingStage::Synchronize);
    if(state_.page!=kShellView||!applications_)return true;
    Transition transition(*this);std::string error;
    if(applications_->foreground_current(foreground(state_.page)))return true;
    if(applications_->switch_to(foreground(state_.page),error))return true;
    log_ui("desktop","desktop foreground transition rejected: "+error,UiLogLevel::Warning);return false;
  }

void DesktopUiHost::persist()  {
    InputWorkScope scope(InputWorkTimingStage::Persistence);
    auto &home=state_.desktop.home;const auto now=state_.desktop.now_ms;
    if(lunaros::graphics::metrics::native&&home.enabled&&home.dirty&&!home.contact&&!home.dragging&&
       (!home.save_failed||uint32_t(now-home.save_attempt_ms)>=5000)){
      home.save_attempt_ms=now;
      const auto previous_error=home.error;
      if(!home.persist(save_home_layout)){home.error="桌面保存失败，稍后重试";log_ui("desktop","Desktop arrangement could not be saved",UiLogLevel::Warning);}
      else if(home.error=="桌面保存失败，稍后重试")home.error.clear();
      if(home.error!=previous_error)state_.force_redraw=true;
    }
    auto &compact=state_.desktop.compact;
    if(!lunaros::graphics::metrics::native&&compact.enabled&&compact.dirty&&!compact.contact&&!compact.dragging&&(!compact.save_failed||uint32_t(now-compact.save_attempt_ms)>=5000)){
      compact.save_attempt_ms=now;const auto previous=compact.error;
      if(!compact.persist(save_compact_layout)){compact.error="桌面保存失败，稍后重试";log_ui("desktop","Compact desktop could not be saved",UiLogLevel::Warning);}
      else if(compact.error=="桌面保存失败，稍后重试")compact.error.clear();
      if(previous!=compact.error)state_.force_redraw=true;
    }
    if(!persist_desktop_preferences(state_.desktop,save_desktop_preferences))
      log_ui("desktop","Desktop settings could not be saved",UiLogLevel::Warning);
  }
}

namespace lunaredu::desktop {
void DesktopUiHost::action(const char *id){
  if(!id)return;
  auto &s=state_.desktop;auto &h=s.home;
  if(std::strcmp(id,"desktop.uninstall")){shell_action(id);return;}
  std::string error;
  if(home_uninstall_){state_.force_redraw=true;return;}
  auto operation=std::make_shared<HomeUninstallOperation>();
  if(!operation->request(s,home_maintenance_,error)){
    if(h.enabled)h.error="卸载失败，请稍后重试";else s.compact.error="卸载失败，请稍后重试";
    state_.force_redraw=true;log_ui("desktop","Application uninstall rejected: "+error,UiLogLevel::Warning);return;
  }
  home_uninstall_=std::move(operation);
  state_.force_redraw=true;
}
}

namespace lunaredu::desktop {
void DesktopUiHost::home() { (void)enter_ui({kShellView,UiEntryKind::Home}); }
bool DesktopUiHost::ready_ui() const {return state_.ready;}
app::ViewRoute DesktopUiHost::ui_route() const {return state_.page;}
app::ViewRoute DesktopUiHost::previous_ui_route() const {return state_.navigation.previous;}
bool DesktopUiHost::take_ui_redraw() {const bool value=state_.force_redraw;state_.force_redraw=false;return value;}
void DesktopUiHost::request_ui_redraw() {state_.force_redraw=true;}
bool DesktopUiHost::application_entered(UiEntry entry) {
  Transition transition(*this);
  if(state_.page!=entry.route)state_.navigation.previous=state_.page;
  rearm_application();return enter_ui(entry);
}
void DesktopUiHost::select_application(const char *id,bool remember) {
  state_.desktop.select(state_.desktop.find_app(id));
  if(remember){state_.navigation.launcher_index=int(state_.desktop.focus);state_.navigation.menu=nullptr;}
}
void DesktopUiHost::display_application(const std::string &id) {state_.displayed_application=id;}
std::string DesktopUiHost::displayed_application_title() const {
  const auto *item=drawing_application(state_.desktop,state_.displayed_application.c_str());
  return item?item->name:"应用";
}
bool DesktopUiHost::catalog_publish_allowed() const {
  const auto &h=state_.desktop.home;
  const auto &c=state_.desktop.compact;
  return state_.page==kShellView&&state_.desktop.surface==DesktopSurface::Launcher&&!state_.desktop.home_navigation.moving&&
      (lunaros::graphics::metrics::native||!c.enabled||(!c.contact&&!c.dragging&&(c.mode==CompactMode::Desktop||c.mode==CompactMode::Edit||c.mode==CompactMode::AllApps)))&&
      (!h.enabled||(!h.contact&&!h.dragging&&(h.mode==HomeMode::Desktop||h.mode==HomeMode::AllApps||h.mode==HomeMode::Edit)));
}
bool DesktopUiHost::publish_catalog(std::shared_ptr<const DesktopApplicationCatalog> catalog) {
  if(!catalog||!state_.desktop.replace_applications(catalog->data(),catalog->size()))return false;
  state_.catalog=std::move(catalog);state_.force_redraw=true;return true;
}
UiPoint DesktopUiHost::logical_point(int x,int y) const {const auto point=state_.desktop.logical(x,y);return {point.x,point.y};}
TouchEvent DesktopUiHost::update_touch(const TouchInput &sample,int64_t now,bool pull) {return state_.touch_router.update(sample,now,pull);}
int DesktopUiHost::pull_refresh_distance(int64_t now) const {return state_.touch_router.pull_refresh_distance(now);}
bool DesktopUiHost::tap(int x,int y) {return ui_graphics().tap(x,y);}
bool DesktopUiHost::take_action(lunaros::graphics::Action &action) {return ui_graphics().take_action(action);}
bool DesktopUiHost::focus_ui(lunaros::graphics::FocusCommand command){
  const bool accepted=ui_graphics().focus(command);
  // Queue overflow also invalidates the page and needs the recovery wake.
  wake_ui();return accepted;
}
void DesktopUiHost::invalidate_input() {ui_graphics().invalidate();state_.touch_router.cancel();}
void DesktopUiHost::unavailable(const char *title,const char *reason) {
  rearm_ui();state_.unavailable_title=title;state_.unavailable_reason=reason;(void)enter_ui({kUnavailableView});
}
void DesktopUiHost::log_ui(const char *owner,const std::string &record,UiLogLevel) {std::printf("%s %s\n",owner,record.c_str());}
}

namespace lunaredu::desktop {
namespace {
#if defined(ESP_PLATFORM) && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
EXT_RAM_BSS_ATTR
#endif
InputWorkTiming input_timing;
system::ElapsedWorkReporter<size_t(InputWorkTimingStage::Count)> input_reporter;
bool input_collecting=false;
void set_input_collection(bool value){
  if(value==input_collecting)return;
  input_timing.enable(value);input_collecting=value;
}
bool measuring_input() {
#ifdef ESP_PLATFORM
  return system::main_work_profiling_active();
#else
  return false;
#endif
}
template<class Action> decltype(auto) measure(InputWorkTimingStage stage,Action &&action) {
  return input_timing.measure_profiled(size_t(stage),[]{
#ifdef ESP_PLATFORM
    return uint64_t(esp_timer_get_time());
#else
    return uint64_t(0);
#endif
  },[]{
#ifdef ESP_PLATFORM
    return system::settled_current_task_runtime_us();
#else
    return uint32_t(0);
#endif
  },std::forward<Action>(action));
}
}
InputWorkTiming::Snapshot read_input_work_timing() {
  set_input_collection(measuring_input());return input_timing.snapshot();
}
InputWorkScope::InputWorkScope(InputWorkTimingStage stage):stage_(stage),active_(measuring_input())
#if defined(ESP_PLATFORM) && defined(LUNAROS_DIAGNOSTIC_PROBES)
,function_scope_(static_cast<system::probe::Point>(unsigned(system::probe::Point::InputBegin)+unsigned(stage)))
#endif
{
#ifdef ESP_PLATFORM
  if(active_){task_=system::settled_current_task_runtime_us();start_=esp_timer_get_time();}
#endif
}
InputWorkScope::~InputWorkScope() {
#ifdef ESP_PLATFORM
  if(active_){const uint32_t task=system::settled_current_task_runtime_us();const uint64_t end=esp_timer_get_time();
    input_timing.record(size_t(stage_),end-start_,end,uint32_t(task-task_));}
#endif
}
  void UiInputHost::update(UiInputSample sample,UiInputPort &port) {
    if(!state_.ready)return;
    set_input_collection(measuring_input());
    input_reporter.report("desktop.input",input_timing.snapshot(),input_work_stage_names);
    measure(InputWorkTimingStage::Begin,[&]{port.begin_tick();if(port.take_redraw())port.redraw();});
    const bool touch_changed=state_.touch_available!=sample.touch.available;
    state_.touch_available=sample.touch.available;
    if(touch_changed)state_.force_redraw=true;
    if(measure(InputWorkTimingStage::Safety,[&]{return port.safety();}))return;
    if(touch_changed)port.contact_layout_changed();
    const auto navigation=measure(InputWorkTimingStage::Navigation,[&]{return port.navigate();});if(navigation.consumed)return;
    session_.released(sample.controls.buttons==0,port.driving_view());
    port.observe_release(sample.controls.buttons==0);
    sample.controls.edge_back=navigation.edge_back_finished;
    sample.controls.now_ms=uint32_t(sample.now_us/1000);
    const auto controls=measure(InputWorkTimingStage::Controls,[&]{return session_.controls(sample.controls);});
    UiInputEvents events{controls.confirm,controls.back,navigation.top_owned,navigation.edge_back_finished,true,controls.encoder};
    events.combo=controls.combo;
    measure(InputWorkTimingStage::Destination,[&]{port.prepare_destination();});
    events.command=measure(InputWorkTimingStage::Command,[&]{return port.command(navigation.top_system);});
    port.widgets(events.confirm,events.back);
    if(measure(InputWorkTimingStage::Application,[&]{return port.dispatch(events);})||
        measure(InputWorkTimingStage::Shell,[&]{return port.shell_input(events,touch_changed);}))return;
    // Unconsumed logical commands follow the same foreground input contract
    // as physical controls. Shell and system navigation keep first refusal.
    int command_navigation=0;
    if(!sample.blocked){
      switch(static_cast<DesktopCommand>(events.command)){
        case DesktopCommand::Confirm:events.confirm=true;break;
        case DesktopCommand::Back:events.back=true;break;
        case DesktopCommand::Next:command_navigation=1;break;
        case DesktopCommand::Previous:command_navigation=-1;break;
        default:break;
      }
    }
    const bool tracking=state_.touch_router.tracking();
    const auto touch=measure(InputWorkTimingStage::Pointer,[&]{return session_.pointer(sample.touch,sample.now_us,sample.blocked,events.top_owned,
        sample.controls.buttons!=state_.last_buttons||sample.controls.encoder_position!=state_.last_encoder_position||events.command>=0);});
    if(touch.gesture!=TouchGesture::None||(tracking&&!state_.touch_router.tracking()))port.pointer_ended(touch);
    int touch_navigation=command_navigation;
    if(!events.confirm&&!events.back&&touch.gesture==TouchGesture::Tap&&port.widgets_enabled()) {
      int x=touch.x,y=touch.y;
      if(port.rotation()){const auto point=shell_.logical(x,y);x=int(point.x);y=int(point.y);}
      if(graphics_.tap(x,y))port.wake();
      port.widgets(events.confirm,events.back);
    } else if(touch.gesture==TouchGesture::Next)touch_navigation=1;
    else if(touch.gesture==TouchGesture::Previous)touch_navigation=-1;
    events.navigation=session_.navigation_delta(events.confirm,events.back,touch_navigation,
        events.encoder,port.axis_navigation()&&sample.axes_fresh&&!sample.blocked,sample.horizontal,
        sample.vertical,uint32_t(sample.now_us/1000),port.rotation());
    events.pointer=touch;events.pointer_tracking=state_.touch_router.tracking();
    events.primary=false;measure(InputWorkTimingStage::Application,[&]{(void)port.dispatch(events);});
    session_.observe(sample.controls.buttons,sample.controls.encoder_position);
  }
}
