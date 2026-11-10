#include "app/ui_owner.h"
#include "lunaredu_pet_snapshot.h"
#include "lunaredu_pet_catalog.h"
#include "app/setup_session.h"
#include "lunaredu_qr_payload.h"
#include "runtime/runtime.h"
#include <algorithm>
namespace lunaredu::pet {
void detach_ui_presentation(){close_setup_presentation();}
UiDecoration ui_decoration(Page page){
  UiDecoration result;
  if(page==Page::Appearance){const auto pet=snapshot();result.shape=pet.presentation.shape;result.color=pet.presentation.color;result.autonomous_appearance_enabled=pet.autonomous_appearance_enabled;}
  SetupPresentation connection;
  if(setup_presentation(connection)){
    result.connection_open=true;
    result.connection_busy=connection.phase==SetupPhase::Preparing || connection.phase==SetupPhase::Cancelling;
    result.connection_title=connection.kind==SetupKind::Account?"账号配对":"网络配置";
    if(connection.phase==SetupPhase::Waiting)result.connection_url=connection.url;
    result.connection_hint=setup_status_caption();
    if(connection.kind==SetupKind::Network && connection.phase==SetupPhase::Waiting && !connection.ssid.empty()){
      result.connection_next=true;
      if(!ui_snapshot().network_setup_link){result.connection_title="1 · 连接设备热点";
        result.connection_hint=connection.ssid;
        result.connection_url=lunaros::qr::wifi_payload(connection.ssid,connection.password);
      }else {result.connection_title="2 · 打开配网页面";result.connection_hint="连接热点后扫码，选择家庭网络";}
    }
  }
  return result;
}
bool apply_local_ui_action(UiAction action,std::string &message){
  if(action>=UiAction::ToggleFlexible&&action<=UiAction::ToggleAgent){
    const auto feature=static_cast<Feature>(unsigned(action)-unsigned(UiAction::ToggleFlexible));
    if(!(ui_snapshot().feature_support&(1U<<unsigned(feature)))){message="当前设备未接入该功能";return false;}
    const bool okay=set_feature_enabled(feature,!feature_preferences().enabled(feature),message);
    message=okay?"设置已更新":"设置未保存，请重试";return okay;
  }
  if(action==UiAction::SaveDefaultMode){
    const bool okay=set_display_mode(ui_snapshot().mode,message);message=okay?"已保存默认显示模式":"默认模式未保存，本次显示仍可使用";return okay;
  }
  if(action==UiAction::CloseConnection){close_setup_presentation();message.clear();return true;}
  if(action==UiAction::PairAccount || action==UiAction::ConfigureNetwork || action==UiAction::RetryConnection){
    auto kind=action==UiAction::ConfigureNetwork?SetupKind::Network:SetupKind::Account;
    if(action==UiAction::RetryConnection){SetupPresentation current;if(!setup_presentation(current))return false;
      if(current.phase==SetupPhase::Preparing || current.phase==SetupPhase::Cancelling){message="连接操作进行中";return false;}kind=current.kind;}
    const bool okay=request_setup(kind,message);if(!okay)message="连接暂不可用，请检查系统设置";return okay;
  }
  if(action==UiAction::CycleShape || action==UiAction::CycleColor || action==UiAction::AutonomousAppearance){
    appearance::Update update;update.mode_explicit=true;update.mode=action==UiAction::AutonomousAppearance?PetAppearanceMode::Autonomous:PetAppearanceMode::Locked;
    const auto state=snapshot();
    if(action==UiAction::CycleShape){const auto &items=catalog::kCharacterShapes;const auto found=std::find_if(items.begin(),items.end(),[&](const char *s){return state.presentation.shape==s;});update.shape=items[(found==items.end()?0:size_t(found-items.begin())+1)%items.size()];}
    if(action==UiAction::CycleColor){const auto &items=catalog::kCharacterColors;const auto found=std::find_if(items.begin(),items.end(),[&](const char *s){return state.presentation.color==s;});update.color=items[(found==items.end()?0:size_t(found-items.begin())+1)%items.size()];}
    const bool okay=runtime::update_appearance(update,message);message=okay?"外观已更新":"外观未保存，请稍后重试";return okay;
  }
  return false;
}
}
