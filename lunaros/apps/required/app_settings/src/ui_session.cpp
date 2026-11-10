#include "lunaros_settings_ui_session.h"

namespace lunaredu::settings {
void UiPresenter::paint(Detail detail,const SystemPresentation &model,const char *version,const char *device,
      desktop::ProvisioningStep step,desktop::UiPaintPort &port,bool auto_hide,unsigned focus) {
    if(!model.provisioning||model.updating||detail==Detail::Update||detail==Detail::Platform)view_.clear();
    view_.paint(detail,model,version,device,step,port.rotation(),port,auto_hide,focus);
  }

void UiPresenter::inactive() { view_.clear(); }

bool UiSession::poll_bluetooth(int64_t now_us) {
    if(system_detail!=Detail::Bluetooth)return false;
    const bool confirming=bluetooth_.snapshot()&&bluetooth_.snapshot()->confirm_pending;
    const bool changed=bluetooth_.poll(now_us,bluetooth_error_);
    if(changed&&!confirming&&bluetooth_.snapshot()->confirm_pending)bluetooth_scroll_=0;
    return changed;
  }

bool UiSession::open_detail(const char *action) {
    if (!detail_for_action(action, system_detail)) return false;
    operation_message_.clear();
    bluetooth_error_.clear();bluetooth_scroll_=0;
    focus=0;
    provisioning_step = desktop::ProvisioningStep::Device;
    return true;
  }

UiUpdate UiSession::update(const SystemPresentation &model, bool confirm, bool back,
                  int navigation, int64_t now_us,uint32_t widget) {
    if(widget==110)widget=10;
    if(!model.updating&&system_detail==Detail::Bluetooth){
      const auto snapshot=model.bluetooth?model.bluetooth:bluetooth_.snapshot();
      if(snapshot&&snapshot->available){
        if(back){focus=1;system_detail=Detail::Network;return {false,true};}
        if(navigation){
          focus=uint8_t((int(focus)+4+navigation%4)%4);
          const auto visible=[&](unsigned value){
            return value==0||(value==1&&snapshot->pairing_supported)||
              (value==2&&(snapshot->confirm_pending||snapshot->ble_scan_supported))||
              (value==3&&(snapshot->confirm_pending||snapshot->classic_supported));
          };
          for(unsigned skip=0;!visible(focus)&&skip<3;++skip)
            focus=uint8_t((int(focus)+4+(navigation>0?1:-1))%4);
          const int pitch=lunaros::graphics::metrics::min_touch+lunaros::graphics::metrics::gap;
          bluetooth_scroll_=focus>1?unsigned(pitch*(snapshot->confirm_pending?2:1)):0;
          return {false,true};
        }
        if(confirm&&!widget){
          static constexpr uint32_t normal[]={10,260,261,262},pairing[]={10,260,263,264};
          widget=snapshot->confirm_pending?pairing[focus%4]:normal[focus%4];
        }
      }
      if(snapshot&&snapshot->available&&!model.network_busy&&snapshot->enabled){
        std::optional<UiCommand> command;
          if(widget==260&&snapshot->pairing_supported)command=UiCommand::BluetoothPair;
          if(widget==261&&snapshot->ble_scan_supported&&!snapshot->discovering)command=UiCommand::BluetoothScanBle;
          if(widget==262&&snapshot->classic_supported&&!snapshot->discovering)command=UiCommand::BluetoothScanClassic;
        if(widget==263&&snapshot->confirm_pending)command=UiCommand::BluetoothConfirmPair;
        if(widget==264&&snapshot->confirm_pending)command=UiCommand::BluetoothRejectPair;
        if(command){bluetooth_error_.clear();return {false,true,command};}
      }
      if(widget>=260&&widget<=264)return {false,true};
    }
    if(!model.updating&&(system_detail==Detail::Network||system_detail==Detail::Bluetooth||system_detail==Detail::ProvisionControl)){
      if(back){
        if(system_detail==Detail::Network)return {true};
        focus=system_detail==Detail::Bluetooth?1:2;system_detail=Detail::Network;return {false,true};
      }
      const unsigned count=system_detail==Detail::Network?3:system_detail==Detail::ProvisionControl?2:1;
      if(navigation)focus=uint8_t((int(focus)+int(count)+navigation%int(count))%int(count));
      if(widget>=10&&widget<10+count){focus=widget-10;confirm=true;}
      if(confirm){
        if(system_detail==Detail::Network){
          if(!focus)return {false,false,{},true};
          if(focus==2&&!model.provisioning_available)return {};
          system_detail=focus==1?Detail::Bluetooth:Detail::ProvisionControl;focus=0;return {false,true};
        }
        if(!model.network_busy){
          if(system_detail==Detail::Bluetooth&&model.bluetooth_available)return {false,true,model.bluetooth_enabled?UiCommand::BluetoothOff:UiCommand::BluetoothOn};
          if(system_detail==Detail::ProvisionControl&&model.provisioning_available){
            if(!focus)return {false,true,model.provisioning?UiCommand::StopProvisioning:UiCommand::StartProvisioning};
            if(model.provisioning){system_detail=Detail::Provision;provisioning_step=desktop::ProvisioningStep::Device;return {false,true};}
          }
        }
      }
      if(navigation||now_us-last_refresh_us_>=500000){last_refresh_us_=now_us;return {false,true};}
      return {};
    }
    if(back&&!model.updating&&system_detail==Detail::Provision){system_detail=Detail::ProvisionControl;focus=1;return {false,true};}
    const auto value = update_detail_interaction(system_detail, provisioning_step,
        model, confirm, back, navigation, now_us, last_refresh_us_);
    std::optional<UiCommand> command;
    if(value.start_provisioning&&!model.network_busy)command=UiCommand::StartProvisioning;
    if(confirm&&!model.updating) {
      if(system_detail==Detail::Platform&&!model.platform_bound&&model.wifi_connected&&model.platform_available&&!model.pairing_pending)command=UiCommand::GeneratePairing;
      if(system_detail==Detail::Update&&model.wifi_connected&&model.platform_available&&model.update_check!=SystemPresentation::UpdateCheck::Checking)
        command=model.update_check==SystemPresentation::UpdateCheck::Available?UiCommand::InstallUpdate:UiCommand::CheckUpdate;
    }
    if(command)operation_message_.clear();
    return {value.leave,value.repaint,command};
  }

RenderPresentation UiSession::presentation(const SystemPresentation &model,
                                  const char *version, const char *device,bool auto_hide) {
    auto_hide_status=auto_hide;
    auto view=model;
    if(system_detail==Detail::Bluetooth){view.bluetooth=bluetooth_.snapshot()?bluetooth_.snapshot():model.bluetooth;view.bluetooth_scroll=bluetooth_scroll_;}
    else {view.bluetooth.reset();view.bluetooth_scroll=0;}
    const bool network=system_detail==Detail::Network||system_detail==Detail::Bluetooth||system_detail==Detail::ProvisionControl||system_detail==Detail::Provision;
    view.operation_message=network&&model.network_busy?"正在处理网络设置":network&&model.network_failed?"网络操作失败，请重试":operation_message_;
    return {std::move(view), *this, version ? version : "", device ? device : "", &presenter_};
  }

void UiSession::command_result(bool ok,const std::string &error) {
    operation_message_=ok?"": "操作未完成，请重试";
    if(system_detail==Detail::Bluetooth)bluetooth_error_=ok?"":error;
  }
}
