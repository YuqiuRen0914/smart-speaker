#pragma once
#include <type_traits>
#include <utility>
#include "desktop/composition/applications.h"
#include "desktop/composition/selection.h"
#include "lunaros_settings_application.h"
#include "lunaros_settings_wifi_runtime.h"
#include "lunaros_settings_ui_session.h"
#include "lunaros_settings_menu.h"
#include "lunaros_settings_desktop_commands.h"
namespace lunaredu::settings {
namespace desktop_detail {
template<class M,class=void>struct HasSystemVersion:std::false_type{};
template<class M>struct HasSystemVersion<M,std::void_t<decltype(std::declval<M>().system_observation.source),decltype(std::declval<M>().system_observation.revision)>>:std::true_type{};
template<class T,class=void>struct HasSettingsCommand:std::false_type {};
template<class T>struct HasSettingsCommand<T,std::void_t<decltype(&T::settings_command)>>:std::true_type {};
template<class Model,class = void> struct HasAccountView : std::false_type {};
template<class Model> struct HasAccountView<Model,std::void_t<decltype(std::declval<Model>().account_avatar)>> : std::true_type {};
}

template<class Host> class SelectedDesktopSession : public lunaros::DesktopSessionDefaults {
 public:
  using Configuration = typename Host::Configuration;
  explicit SelectedDesktopSession(Host &host) : host_(host) {settings_.bind_bluetooth(bluetooth_observation_ports());}
  SelectedDesktopSession(const SelectedDesktopSession &) = delete;
  SelectedDesktopSession &operator=(const SelectedDesktopSession &) = delete;
  bool widgets() const {return host_.state().page==kDetailView;}
  std::optional<app::ViewRoute> destination() const {
    if (auto update = forced_destination<Configuration>(host_.model().updating, host_.state().page)) return update;
    if(return_from_wifi_&&host_.state().page==desktop::kShellView){
      const auto &shell=host_.state().desktop;
      if(shell.surface==desktop::DesktopSurface::Actions&&shell.active_menu&&!std::strcmp(shell.active_menu->id,"os.settings")){
        return_from_wifi_=false;settings_.open_detail("os.network");return kDetailView;
      }
      if(shell.surface!=desktop::DesktopSurface::Wifi)return_from_wifi_=false;
    }
    return std::nullopt;
  }
  void bind(lunaros::ApplicationContext *device){device_=device;}
  void begin_tick(){
    if constexpr(desktop_detail::HasAccountView<std::decay_t<decltype(host_.model())>>::value){
      const auto &model=host_.model();auto &shell=host_.state().desktop;
      if constexpr(desktop_detail::HasSystemVersion<std::decay_t<decltype(model)>>::value){
        const auto &v=model.system_observation;
        if(v.source&&account_source_==v.source&&account_revision_==v.revision&&menu_configuration_==shell.menu_configuration_generation)return;
        account_source_=v.source;account_revision_=v.revision;menu_configuration_=shell.menu_configuration_generation;
      }
      std::string_view name;
      if(model.platform_bound)name=model.account_name.empty()?std::string_view("已配对"):std::string_view(model.account_name);
      // Bound UTF-8 by complete characters; a long name must not lose the row.
      constexpr size_t maximum=sizeof(desktop::DesktopMenuIdentity::title)-1;
      if(name.size()>maximum){size_t end=maximum;while(end&&((uint8_t(name[end])&0xc0)==0x80))--end;name=name.substr(0,end);}
      bool changed=shell.set_menu_value("os.platform",model.platform_bound?">":"未配对");
      changed|=shell.set_menu_identity("os.platform",name,model.platform_bound?model.account_avatar:nullptr);
      if(changed)host_.state().force_redraw=true;
    }
  }
  void poll(bool allowed){
    if(allowed&&host_.state().page==kDetailView&&settings_.poll_bluetooth(int64_t(Configuration::now_ms())*1000))
      host_.publish(desktop::RenderKind::Full,&host_.model());
    const auto command=pending_command_; pending_command_.reset();
    if(!command||!allowed||!device_||!app::view_owned_by(host_.state().page,kApplicationId))return;
    std::string error;
    bool ok=false;
    if constexpr(desktop_detail::HasSettingsCommand<Configuration>::value)ok=apply_desktop_command(*device_,*command,error,Configuration::settings_command);
    else ok=apply_desktop_command(*device_,*command,error);
    settings_.command_result(ok,error);
    if(!ok)Configuration::log(error,desktop::UiLogLevel::Warning);
  }
  bool shell_action(const char *id) {
    if(!std::strcmp(id,"os.wallpaper.file"))return host_.ui().launch_ui("os.files",false);
    if (!settings_.open_detail(id)) return false;
    host_.change_page(kDetailView, host_.model()); return true;
  }
  template<class Base> struct Context : Base {
    Context(Base base, SelectedDesktopSession &session) : Base(std::move(base)), owner(session) {}
    auto &settings_session() { return owner.settings_; }
    const char *software_version() const { return Configuration::version(); }
    const char *device_name() const { return Configuration::name; }
    bool settings_auto_hide_status() const {
      return owner.host_.state().desktop.preferences.auto_hide_status;
    }
    uint32_t settings_widget_action() {
      uint32_t widget=0; lunaros::graphics::Action action;
      while(owner.host_.ui().take_action(action)) {
        if(action.id==269){owner.settings_.scroll_bluetooth(action.value);this->host.publish(desktop::RenderKind::Full,&this->model);}
        else widget=action.id;
      }
      return widget;
    }
    template<class Update> void apply_settings(const Update &update) {
      if(update.open_wifi){
        // Commit the App detail exit first: the OS restores its remembered
        // menu on this transition. Open WiFi only after that restoration.
        this->host.change_page(desktop::kShellView,this->model);
        auto &shell=this->host.state().desktop;
        shell.open_menu("os.settings");shell.leaf_menu=shell.active_menu;shell.leaf_row=0;
        desktop::open_desktop_wifi(shell);owner.return_from_wifi_=true;
        this->host.change_page(desktop::kShellView,this->model);
      }
      else if (update.leave) {
        if(owner.settings_.system_detail==Detail::Network)this->host.state().desktop.open_menu("os.settings");
        this->host.change_page(desktop::kShellView, this->model);
      }
      else if (update.command) owner.pending_command_=update.command;
      else if (update.repaint) { this->host.record_input(this->model); this->host.publish(desktop::RenderKind::Full, &this->model); }
    }
    SelectedDesktopSession &owner;
  };
 private:
  const void *account_source_=nullptr;uint64_t account_revision_=0;uint32_t menu_configuration_=0;
  Host &host_;
  lunaros::ApplicationContext *device_=nullptr;
  std::optional<UiCommand> pending_command_;
  mutable UiSession settings_;
  mutable bool return_from_wifi_ = false;
};
struct DesktopRegistration {
  bool operator()(Application &application,app::AppController &controller,lunaros::DesktopApplicationPorts ports,std::string &error) const {
    return application.declare(controller,{ports.context,ports.prepare,ports.enter,ports.leave,ports.shell,ports.now_ms,ports.data},error);
  }
};
inline lunaros::DesktopAppDescriptor desktop_registration() {
  return {kApplicationId,lunaros::DesktopAppOwner<Application,DesktopRegistration>::create,nullptr,
      [](lunaros::KernelRuntime &runtime){desktop::configure_desktop_wifi(&runtime);},
      [](desktop::DesktopShell &shell,uint32_t now,bool visible){return desktop::update_desktop_wifi(shell,now,visible);}};
}
struct DesktopEntry : lunaros::DesktopEntryDefaults {
  using Presentation = RenderPresentation;
  template<class Host> using Session = SelectedDesktopSession<Host>;
  static constexpr size_t menu_count = std::size(desktop::settings_menu::menus);
  static auto menus() { return desktop::settings_menu::menus; }
  static auto tile() { return desktop_application(); }
  static auto provider() { return desktop_registration(); }
  template<class Bindings> static auto binding() { return ui_binding<Bindings>(); }
};
}
