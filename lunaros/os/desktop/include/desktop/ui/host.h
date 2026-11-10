#pragma once
#include "display/screen_status.h"
#include "desktop/ui/entry.h"
#include "desktop/ui/host_state.h"
#include "desktop/ui/host_port.h"
#include "desktop/ui/input_host.h"
#include "desktop/ui/input_timing.h"
#include "desktop/shell/interaction.h"
#include "desktop/application/foreground_route.h"
#include "desktop/application/launch.h"
#include <atomic>
#include "desktop/graphics/canvas_spec.h"
#include "app/package/application_data_ports.h"
namespace lunaredu::app { class AppController; }
namespace lunaredu::lunaros { struct DesktopCompositionAccess; template<class,class> class DesktopSessionHost; }
namespace lunaredu::desktop {
struct HomeUninstallOperation;
// Optional App observers are assembled by the default entry, never imported
// into this generic host. Hidden/blocked surfaces retire their observation.
class ShellObservers {
 public:
  virtual ~ShellObservers()=default;
  virtual bool poll(DesktopShell &,uint32_t,bool visible)=0;
};
struct DesktopUiOptions {
  const char *shell_id;
  const char *settings_id="os.settings";
  const DesktopMenuOwner *menus=nullptr;
  size_t menu_count=0,builtin_count=0;
  DesktopRenderOptions rendering{};
  const char *default_startup = nullptr;
  // A boot coordinator launches the preference after the first Home transfer.
  bool defer_startup_application = false;
  const HomeWidgetDefinition *home_widgets = nullptr;
  size_t home_widget_count = 0;
};
struct DesktopUiSample {
  UiInputSample input;
  int vertical=0;
  bool system_pressed=false,changed=false;
  bool wifi_connected=false,platform_available=false,platform_connected=false;
  const char *wifi_ssid="";
};
struct DesktopUiPolicy {
  bool driving=false,axis_navigation=false,orientation_locked=false;
  bool navigation_allowed=true,application_widgets=false;
  bool attention=false,low_battery=false;
};
// A usable default displayed host: one mailbox, checked foreground/route
// admission, widgets, gestures, Shell interaction and preference persistence.
// App hooks describe business policy; no product model, physical key, worker
// or selected App is owned here. Calls remain on the existing input owner.
class DesktopUiHost : public UiHostPort,private UiInputPort,
                      private ShellNavigationPort,private ShellInteractionPort {
 public:
  DesktopUiHost(UiHostState &state,DesktopUiOptions options,ShellObservers *observers=nullptr)
      :state_(state),options_(options),observers_(observers),session_(state),
       navigation_({state.desktop,state.touch_router,state.page,state.wait_for_button_release,state.force_redraw},*this){
    state_.desktop.settings_id = options_.settings_id;
  }
  DesktopUiHost(const DesktopUiHost &)=delete;
  DesktopUiHost &operator=(const DesktopUiHost &)=delete;
  UiSession &session() {return session_;}
  // Forward a producer's retained redraw without advancing input or App hooks.
  bool service_redraw();
  void bind_applications(app::AppController &applications) {applications_=&applications;}
  void restore_preferences(bool preserve_missing_startup=false);
  void bind_home_maintenance(lunaros::apps::ApplicationDataPorts ports) {home_maintenance_=ports;}
  void project_foreground(const char *id) {
    if(transitioning_||!state_.ready)return;
    Transition transition(*this);foreground_entered(id);
  }
  bool ready_ui() const final;
  UiAppearance ui_appearance() const final {
    return {state_.desktop.preferences.auto_hide_status,state_.desktop.preferences.antialias};
  }
  app::ViewRoute ui_route() const final;
  app::ViewRoute previous_ui_route() const final;
  bool take_ui_redraw() final;
  void request_ui_redraw() final;
  void select_application(const char *id,bool remember_location=false) final;
  void display_application(const std::string &id) final;
  std::string displayed_application_title() const final;
  bool catalog_publish_allowed() const final;
  bool publish_catalog(std::shared_ptr<const DesktopApplicationCatalog>) final;
  UiPoint logical_point(int x,int y) const final;
  TouchEvent update_touch(const TouchInput &,int64_t,bool) final;
  int pull_refresh_distance(int64_t) const final;
  bool tap(int x,int y) final;
  bool take_action(lunaros::graphics::Action &) final;
  bool focus_ui(lunaros::graphics::FocusCommand) final;
  void home() final;
  void invalidate_input() final;
  void unavailable(const char *title,const char *reason) final;
  void log_ui(const char *owner,const std::string &record,UiLogLevel=UiLogLevel::Info) override;
  bool queue_command(DesktopCommand value) final;
  bool take_command(DesktopCommand &value) final;
  bool command_pending() const {return commands_.load(std::memory_order_acquire)>=0;}
  void discard_commands() final {commands_.store(-1);}
  void select_widgets();
  void cancel_widgets() final {
    if(ui_graphics().cancel_contact()){state_.force_redraw=true;wake_ui();}
  }
  const char *foreground(app::ViewRoute page) const;
  bool change_page(app::ViewRoute next);
  bool enter_ui(UiEntry entry) final;
  bool application_entered(UiEntry entry) final;
  bool launch_ui(const char *id,bool downloaded=false) final;
  void rearm_ui() override {rearm_application();session_.rearm(state_.desktop,ui_graphics());}
  void update(DesktopUiSample sample);
 protected:
  virtual lunaros::graphics::SceneRuntime &ui_graphics()=0;
  int64_t now_us() const {return sample_.input.now_us;}
  virtual DesktopUiPolicy policy() const {return {};}
  virtual void tick_applications(int64_t) {}
  virtual bool safety() override {return false;}
  virtual void observe_release(bool) override {}
  virtual void prepare_destination() override {}
  virtual bool application_input(UiInputEvents,int64_t) {return false;}
  virtual void shell_action(const char *) {}
  virtual bool launcher_back_shortcut() override {return false;}
  virtual bool launcher_combo_shortcut() {return false;}
  virtual bool back_application() override {return false;}
  virtual void home_admitted() {}
  virtual bool shell_changed() {return false;}
  virtual void sample_observed() {}
  virtual void cancel_application_input() override {}
  virtual void rearm_application() {}
  virtual app::ViewRoute qualify_destination(app::ViewRoute page) {return page;}
  virtual void destination_committed(app::ViewRoute) {}
  virtual void foreground_entered(const char *) {}
  virtual ApplicationLaunchResult launch_application(const char *,std::string &) {return ApplicationLaunchResult::Unknown;}
  virtual bool launch_downloaded(const char *) {return false;}
  virtual bool widget_action(const lunaros::graphics::Action &a,bool &confirm,bool &back) {
    if(a.id==1)confirm=true;else if(a.id==2)back=true;return false;
  }
  virtual void paint_navigation() {render_ui();}
  virtual void request_redraw() {state_.force_redraw=true;}
  virtual bool take_redraw() override {return false;}
  virtual void pointer_ended(const TouchEvent &) override {}
 private:
  lunaros::apps::ApplicationDataPorts home_maintenance_;
  std::shared_ptr<HomeUninstallOperation> home_uninstall_;
  friend struct lunaros::DesktopCompositionAccess;
  template<class,class> friend class lunaros::DesktopSessionHost;
  // Scoped suppression belongs to this host; the mutable guard never escapes.
  template<class Action> decltype(auto) coordinate_foreground(Action action) {
    Transition transition(*this);return action();
  }

  class Transition {
   public:
    explicit Transition(DesktopUiHost &host):host_(host),previous_(host.transitioning_){host.transitioning_=true;}
    ~Transition(){host_.transitioning_=previous_;}
    Transition(const Transition &)=delete;
    Transition &operator=(const Transition &)=delete;
   private:DesktopUiHost &host_;bool previous_;
  };
  DesktopUiPolicy input_policy() const {
    if(!input_pass_||!policy_valid_||policy_route_!=state_.page){
      policy_snapshot_=policy();policy_route_=state_.page;policy_valid_=true;
    }
    return policy_snapshot_;
  }
  void begin_tick() final;
  bool commit_page(app::ViewRoute next,bool restore_previous_shell);
  void redraw() final {render_ui();}
  void contact_layout_changed() final {if(!input_policy().driving&&state_.page!=kShellView)change_page(state_.page);}
  NavigationResult navigate() final;
  bool driving_view() const final {return input_policy().driving;}
  int command(bool system) final;
  void widgets(bool &confirm,bool &back) final;
  bool dispatch(UiInputEvents e) final {
    const bool consumed=application_input(e,sample_.input.now_us);
    policy_valid_=false;return consumed;
  }
  bool shell_input(UiInputEvents e,bool touch_changed) final;
  bool axis_navigation() const final {return state_.page==kShellView||input_policy().axis_navigation;}
  bool widgets_enabled() const final {return state_.page.widgets;}
  unsigned rotation() const final {return state_.page.fixed_orientation||input_policy().orientation_locked?0:state_.desktop.preferences.rotation;}
  void wake() final {wake_ui();}
  bool leave_for_home(std::string &error) final;
  void suspend_observers(uint32_t now) final {if(observers_)observers_->poll(state_.desktop,now,false);}
  bool sync_foreground() final {return synchronize();}
  void show_shell() final {change_page(kShellView);}
  void paint(NavigationPaint paint) final {if(paint==NavigationPaint::Animation)paint_navigation();else render_ui();}
  void rejected(const std::string &error) final {log_ui("desktop","Navigation cleanup pending: "+error,UiLogLevel::Warning);}
  bool launch(const char *id) final {state_.navigation.menu=nullptr;return launch_ui(id);}
  void action(const char *id) final;
  bool synchronize() final;
  void persist() final;
  bool poll(uint32_t now) final {InputWorkScope scope(InputWorkTimingStage::Observers);return observers_&&observers_->poll(state_.desktop,now,state_.page==kShellView);}
  UiHostState &state_;DesktopUiOptions options_;ShellObservers *observers_;
  UiSession session_;ShellNavigation navigation_;app::AppController *applications_=nullptr;
  mutable DesktopUiPolicy policy_snapshot_{};
  mutable app::ViewRoute policy_route_=kShellView;
  mutable bool policy_valid_=false;bool input_pass_=false;
  std::atomic<int> commands_{-1};bool transitioning_=false,shell_widgets_=false,shell_touch_down_=false;DesktopUiSample sample_;
};
}
