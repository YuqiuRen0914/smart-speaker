#pragma once
#include <optional>
#include "desktop/ui/entry.h"
#include "lunaros_settings_views.h"
#include "lunaros_settings_detail_interaction.h"
#include "desktop/ui/paint_port.h"
#include "desktop/ui/render_hooks.h"
namespace lunaredu::settings {
enum class UiCommand { StartProvisioning, GeneratePairing, CheckUpdate, InstallUpdate, StopProvisioning, BluetoothOn, BluetoothOff,
  BluetoothPair, BluetoothScanBle, BluetoothScanClassic, BluetoothConfirmPair, BluetoothRejectPair };
inline desktop::UiEntry ui_entry() { return {desktop::kShellView,desktop::UiEntryKind::Menu,"os.settings"}; }
inline std::optional<app::ViewRoute> forced_destination(bool updating,app::ViewRoute current) {
  return updating&&current!=kDetailView?std::optional<app::ViewRoute>(kDetailView):std::nullopt;
}
template<class Configuration> inline std::optional<app::ViewRoute> forced_destination(bool updating,app::ViewRoute current) {
  if constexpr(requires { Configuration::automatic_update_navigation; })
    if(!Configuration::automatic_update_navigation)return std::nullopt;
  return forced_destination(updating,current);
}
struct UiState {
  Detail system_detail=Detail::Network;
  desktop::ProvisioningStep provisioning_step=desktop::ProvisioningStep::Device;
  uint8_t focus=0;
  bool auto_hide_status=false;
};
class UiPresenter {
 public:
  void paint(Detail detail,const SystemPresentation &model,const char *version,const char *device,
      desktop::ProvisioningStep step,desktop::UiPaintPort &port,bool auto_hide=false,unsigned focus=0);
  void inactive();
 private: DetailView view_;
};
struct RenderPresentation {
  SystemPresentation model;
  UiState state;
  std::string version,device;
  UiPresenter *presenter=nullptr; // Resident, mutated only by the render owner.
  desktop::UiRenderCache render_cache() const {
    return {presenter,[](void *p){if(p)static_cast<UiPresenter *>(p)->inactive();}};
  }
  void paint(desktop::UiPaintPort &port,bool) const {
    if(presenter)presenter->paint(state.system_detail,model,version.c_str(),device.c_str(),state.provisioning_step,port,state.auto_hide_status,state.focus);
  }
};
struct UiUpdate {
  bool leave = false, repaint = false;
  std::optional<UiCommand> command{};
  bool open_wifi=false;
};
// The App owns detail selection, refresh timing and its retained presenter.
// Hosts only submit input facts and apply the resulting navigation/command.
class UiSession : public UiState {
public:
  void bind_bluetooth(BluetoothObservationPorts ports){bluetooth_.bind(ports);}
  bool poll_bluetooth(int64_t now_us);
  void scroll_bluetooth(int32_t value){bluetooth_scroll_=unsigned(std::clamp(value,int32_t(0),int32_t(8192)));}
  bool open_detail(const char *action);
  UiUpdate update(const SystemPresentation &model, bool confirm, bool back,
                  int navigation, int64_t now_us,uint32_t widget=0);
  RenderPresentation presentation(const SystemPresentation &model,
                                  const char *version, const char *device,bool auto_hide=false);
  void command_result(bool ok,const std::string &error);
private:
  BluetoothObservation bluetooth_;
  std::string bluetooth_error_;
  unsigned bluetooth_scroll_=0;
  std::string operation_message_;
  int64_t last_refresh_us_ = 0;
  UiPresenter presenter_; // Mutated only on the render owner, including inactive().
};
template<class Bindings>
typename Bindings::Definition ui_binding() {
  return {"os.settings",
    [](typename Bindings::CaptureContext &c) -> typename Bindings::Presentation {
      return c.settings_session().presentation(c.model,c.software_version(),c.device_name(),c.settings_auto_hide_status());
    }, [](typename Bindings::InputContext &c) {
      if(c.primary) return false;
      const uint32_t widget=c.settings_widget_action();
      c.apply_settings(c.settings_session().update(c.model,c.confirm||widget==1,c.back||widget==2,c.navigation,c.now_us,widget));
      return true;
    }, [](typename Bindings::EntryContext &c) { c.enter_ui(ui_entry()); }};
}

}
