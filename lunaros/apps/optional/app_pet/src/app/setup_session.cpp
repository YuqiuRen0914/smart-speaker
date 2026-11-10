#include "app/setup_session.h"
#include "lunaredu_pet_display_preferences.h"
#include "app/codec/app_capability_json.h"
#include "lunaros_network.h"
#include "lunaros_platform_link_pairing_activity.h"
#if defined(LUNAROS_PET_PROVISIONING_COMPONENT)
#include "lunaros_provisioning_activity.h"
#endif
#include <mutex>
#include <cstring>
#include <esp_timer.h>
namespace lunaredu::pet {
namespace {
std::mutex session_mutex;
SetupKind visible_kind = SetupKind::Account;
bool visible = false;
SetupPresentation cached;
uint32_t observed_at=0;
lunaros::PlatformLinkPairingActivityHandle pairing;
#if defined(LUNAROS_PET_PROVISIONING_COMPONENT)
lunaros::ProvisioningActivityHandle provisioning;
#endif
SetupPhase phase(lunaros::PlatformLinkPairingActivityPhase p) {
  using P = lunaros::PlatformLinkPairingActivityPhase;
  switch(p) { case P::Preparing:return SetupPhase::Preparing; case P::Waiting:return SetupPhase::Waiting;
    case P::Succeeded:return SetupPhase::Succeeded; case P::Failed:return SetupPhase::Failed;
    case P::Cancelling:return SetupPhase::Cancelling; case P::Cancelled:return SetupPhase::Cancelled; }
  return SetupPhase::Failed;
}
#if defined(LUNAROS_PET_PROVISIONING_COMPONENT)
SetupPhase phase(lunaros::ProvisioningActivityPhase p) {
  using P = lunaros::ProvisioningActivityPhase;
  switch(p) { case P::Preparing:return SetupPhase::Preparing; case P::Waiting:return SetupPhase::Waiting;
    case P::Succeeded:return SetupPhase::Succeeded; case P::Failed:return SetupPhase::Failed;
    case P::Cancelling:return SetupPhase::Cancelling; case P::Cancelled:return SetupPhase::Cancelled; }
  return SetupPhase::Failed;
}
#endif
}
bool request_setup(SetupKind kind,std::string &error) {
  std::lock_guard<std::mutex> lock(session_mutex); bool ok = false;
  if(kind == SetupKind::Account) ok = lunaros::begin_platform_link_pairing_activity("pet",pairing,error,true);
  else if(kind == SetupKind::Network) {
#if defined(LUNAROS_PET_PROVISIONING_COMPONENT)
    ok = lunaros::begin_provisioning_activity("pet",provisioning,error);
#else
    error = "provisioning_unavailable";
#endif
  } else { error = "pet_setup_kind_invalid"; }
  if(ok) { visible = true; visible_kind = kind; observed_at=0;cached={}; } return ok;
}
bool cancel_setup(std::string &error) {
  std::lock_guard<std::mutex> lock(session_mutex);
  observed_at=0;cached={};
  if(visible_kind == SetupKind::Account) return lunaros::cancel_platform_link_pairing_activity("pet",pairing,error);
#if defined(LUNAROS_PET_PROVISIONING_COMPONENT)
  return lunaros::cancel_provisioning_activity("pet",provisioning,error);
#else
  error = "provisioning_unavailable"; return false;
#endif
}
void close_setup_presentation() { std::lock_guard<std::mutex> lock(session_mutex); visible = false;observed_at=0;cached={}; }
bool setup_presentation(SetupPresentation &out) {
  std::lock_guard<std::mutex> lock(session_mutex); if(!visible) return false;
  const auto now=uint32_t(esp_timer_get_time()/1000);
  if(observed_at&&uint32_t(now-observed_at)<100){out=cached;return true;}
  std::string error; out = {}; out.kind = visible_kind;
  if(visible_kind == SetupKind::Account) {
    lunaros::PlatformLinkPairingActivitySnapshot state;
    if(!lunaros::read_platform_link_pairing_activity("pet",pairing,state,error)) { visible=false; return false; }
    out.phase=phase(state.phase);out.code=state.pairing.code;out.url=state.pairing.bind_url;
  } else {
#if defined(LUNAROS_PET_PROVISIONING_COMPONENT)
    lunaros::ProvisioningActivitySnapshot state;
    if(!lunaros::read_provisioning_activity("pet",provisioning,state,error)) { visible=false; return false; }
    out.phase=phase(state.phase);out.url=state.url;out.ssid=state.provisioning.ssid;out.password=state.password;
#else
    visible=false;return false;
#endif
  }
  cached=out;observed_at=now?now:1;return true;
}
std::string setup_status_caption() {
  SetupPresentation state;
  if(setup_presentation(state)) {
    if(state.phase==SetupPhase::Succeeded)return state.kind==SetupKind::Account?"配对完成":"网络已连接";
    if(state.phase==SetupPhase::Cancelling)return "正在结束连接操作";
    if(state.phase==SetupPhase::Cancelled)return "连接操作已取消";
    if(state.phase==SetupPhase::Failed)return state.kind==SetupKind::Account?"配对失败，可重试":"配网失败，可重试";
    if(!state.code.empty())return "配对码 "+state.code;
    return state.kind==SetupKind::Account?"正在生成配对码":"配网模式已开启";
  }
  const auto network=lunaros::network_fast_snapshot();
  if(network.link!=lunaros::NetworkLinkState::Up||!network.station_has_address)return "离线可用 · 配网";
  const auto binding=lunaros::platform_link_binding_status();
  if(binding.state!=lunaros::PlatformLinkBindingState::Bound||binding.persistence_pending||binding.revocation_requested)return "未配对 · 配对";
  return {};
}
namespace {
bool settings_read(app::CapabilityRegistry &,void *,const char *,JsonObjectConst,JsonObject out,std::string &){out["fullscreen"]=display_mode()==DisplayMode::Fullscreen;return true;}
bool settings_write(app::CapabilityRegistry &,void *,const char *,JsonObjectConst input,JsonObject out,std::string &error){
  const bool okay=set_display_mode(input["fullscreen"].as<bool>()?DisplayMode::Fullscreen:DisplayMode::Window,error);out["saved"]=okay;return okay;
}
bool setup_command(app::CapabilityRegistry &,void *,const char *,JsonObjectConst input,JsonObject out,std::string &error){
  const char *action=input["action"];
  bool ok=false;
  if(!std::strcmp(action,"pair"))ok=request_setup(SetupKind::Account,error);
  else if(!std::strcmp(action,"network"))ok=request_setup(SetupKind::Network,error);
  else if(!std::strcmp(action,"cancel"))ok=cancel_setup(error);
  else {close_setup_presentation();ok=true;}
  out["accepted"]=ok;return ok;
}
const app::CapabilityField fullscreen[]={{"fullscreen",app::CapabilityFieldType::Boolean}};
const app::CapabilityField saved[]={{"saved",app::CapabilityFieldType::Boolean}};
const app::CapabilityField accepted[]={{"accepted",app::CapabilityFieldType::Boolean}};
constexpr const char *actions[]={"pair","network","cancel","close"};
const app::CapabilityField setup_input[]={{"action",app::CapabilityFieldType::String,true,"连接操作",0,0,1,16,actions,4}};
constexpr const char *callers[]={"product","pet","assistant","device_api","micropython"};
const app::JsonCapabilityDefinition definitions[]={
 {"pet.settings.get","pet",1,"宠物设置","读取宠物显示模式",app::CapabilityExecution::Query,{}, {fullscreen,1},settings_read,nullptr,nullptr,callers,5},
 {"pet.settings.set","pet",1,"宠物设置","保存窗口或全屏模式",app::CapabilityExecution::Command,{fullscreen,1},{saved,1},settings_write,nullptr,nullptr,callers,5},
 {"pet.setup.request","pet",1,"连接与配对","显式申请对应服务操作，并在宠物页面显示进度；关闭呈现不会停止后台操作",app::CapabilityExecution::Command,{setup_input,1},{accepted,1},setup_command,nullptr,nullptr,callers,5}};
}
bool register_setup_capabilities(app::CapabilityRegistry &registry,std::string &error){
  for(const auto &definition:definitions)
    if(!app::register_json_capability(registry,definition,error))return false;
  return true;
}
}
