#include "lunaros_ui_metrics.h"
#include "lunaros_settings_renderer.h"
#include "desktop/graphics/page.h"
#include "desktop/graphics/scene_icon.h"
#include <cstdio>
#include <cmath>
namespace lunaredu::settings {
namespace ui=::lunaredu::lunaros::graphics::metrics;
namespace graphics = lunaros::graphics;
namespace pages = graphics::page;
namespace theme = graphics::theme;
const char *DetailRenderer::title(Detail detail,const SystemPresentation &v) {
  if(v.updating||detail==Detail::Update)return "软件更新";
  return detail==Detail::Platform?"平台配对":detail==Detail::Bluetooth?"蓝牙":
      detail==Detail::ProvisionControl?"手机连接":detail==Detail::Provision?"连接码":"网络";
}
bool DetailRenderer::build(graphics::Scene &scene,Detail detail,const SystemPresentation &v,
    const char *version,const char *device_name,desktop::ProvisioningStep step,DetailActions actions,bool auto_hide,unsigned focus) {
  updating_=v.updating;activity_phase_=v.activity_phase;
  avatar_=detail==Detail::Platform&&v.platform_bound&&!v.updating?v.account_avatar:nullptr;
  overlay_=v.provisioning&&detail==Detail::Provision&&!v.updating;
  setup_link_qr_=overlay_&&!v.setup_url.empty()&&step==desktop::ProvisioningStep::Device;
  if(step==desktop::ProvisioningStep::Device&&!setup_link_qr_)step=desktop::ProvisioningStep::Hotspot;
  if(!overlay_)qr_.clear();
  if(!v.updating&&detail==Detail::Bluetooth&&v.bluetooth&&v.bluetooth->available)
    return build_bluetooth_surface(scene,*v.bluetooth,v.bluetooth_scroll,auto_hide,focus,v.network_busy);
  if(!v.updating&&(detail==Detail::Network||detail==Detail::Bluetooth||detail==Detail::ProvisionControl)){
    const desktop::ContentLayout layout(auto_hide);
    const int height=layout.row_height();
    constexpr uint32_t group=190;
    bool ok=scene.flow(group,{ui::px(8),layout.top,ui::px(224),layout.height()},false,layout.row_pitch()-height);
    const auto row=[&](unsigned index,const char *name,const char *value,lunaros::icons::Icon icon,bool enabled=true){
      const uint32_t id=10+index;
      bool built=pages::row(scene,id,{0,0,ui::px(224),height},name,value,focus==index,enabled,group,theme::Tone::Normal,48);
      built=scene.flow_item(id,0,index)&&built;
      const uint16_t color=index==0?theme::mint:index==1?theme::violet:theme::appaccent;
      return pages::icon_badge(scene,90+index*3,{ui::px(8),(height-ui::px(30))/2,ui::px(30),ui::px(30)},icon,color,id)&&built;
    };
    if(detail==Detail::Network){
      ok=row(0,"Wi-Fi",v.wifi_connected?v.wifi_ssid.c_str():"未连接",lunaros::icons::Icon::Wifi)&&ok;
      ok=row(1,"蓝牙",v.bluetooth_enabled?"开启":"关闭",lunaros::icons::Icon::Bluetooth)&&ok;
      return row(2,"手机连接",!v.provisioning_available?"不可用":v.provisioning?"配网已开":"配网关闭",lunaros::icons::Icon::Link,v.provisioning_available)&&ok;
    }
    const bool bluetooth=detail==Detail::Bluetooth;
    ok=pages::switch_row(scene,10,110,{0,0,ui::px(224),height},bluetooth?"蓝牙":"配网模式",
        bluetooth?v.bluetooth_enabled:v.provisioning,focus==0,!v.network_busy&&(bluetooth?v.bluetooth_available:v.provisioning_available),group,48)&&ok;
    ok=scene.flow_item(10,0,0)&&ok;
    ok=pages::icon_badge(scene,90,{ui::px(8),(height-ui::px(30))/2,ui::px(30),ui::px(30)},bluetooth?lunaros::icons::Icon::Bluetooth:lunaros::icons::Icon::Wifi,bluetooth?theme::violet:theme::mint,10)&&ok;
    if(bluetooth){
      ok=scene.flow(194,{0,0,ui::px(224),height},true,ui::gap,group)&&scene.flow_item(194,0,1)&&ok;
      ok=scene.label(191,{0,0,0,height},"连接状态",ui::font(16),theme::appsecondary,graphics::Align::Left,194)&&scene.flow_item(191,1)&&ok;
      ok=scene.label(192,{0,0,ui::px(112),height},v.bluetooth_connected?"已连接":"未连接",ui::font(14),theme::text,graphics::Align::Right,194)&&ok;
      return scene.label(193,{0,0,ui::px(224),height},v.bluetooth_available?"允许手机通过蓝牙连接":"本设备不支持蓝牙",ui::font(14),theme::appsecondary,graphics::Align::Center,group)&&scene.flow_item(193,0,2)&&ok;
    }
    ok=row(1,"连接码",v.provisioning?"查看 >":"请先开启",lunaros::icons::Icon::Link,v.provisioning&&!v.network_busy)&&ok;
    const char *hint=!v.operation_message.empty()?v.operation_message.c_str():!v.provisioning_available?"当前固件未启用手机配网":!v.setup_error.empty()?"配网未完成，请重试":v.provisioning?(v.provisioning_ble?"可通过蓝牙或热点连接":"通过热点连接后配置 Wi-Fi"):"开启后允许手机配置 Wi-Fi";
    return scene.label(193,{0,0,ui::px(224),height},hint,ui::font(14),v.network_failed?theme::warning:theme::appsecondary,graphics::Align::Center,group)&&scene.flow_item(193,0,2)&&ok;
  }
  if(v.updating) {
    bool ok=scene.label(20,{ui::px(12),ui::px(42),ui::px(216),ui::px(28)},v.update_restarting?"更新完成":"正在更新",ui::font(20),v.update_restarting?theme::success:theme::text);
    if(v.update_progress_known) {
      char progress[24];std::snprintf(progress,sizeof(progress),"%u%%",v.update_percent);
      ok=scene.progress(21,{ui::px(24),ui::px(151),ui::px(192),ui::px(8)},v.update_percent,theme::appaccent)&&ok;
      ok=scene.label(22,{ui::px(12),ui::px(165),ui::px(216),ui::px(24)},progress,ui::font(16),theme::text)&&ok;
    }
    return scene.label(23,{ui::px(12),ui::px(191),ui::px(216),ui::px(24)},v.update_restarting?"即将自动重启":"请保持供电",ui::font(16),theme::text)&&ok;
  }
  if(detail==Detail::Update) {
    const desktop::ContentLayout layout(auto_hide);
    const auto check=v.update_check;
    const bool available=check==SystemPresentation::UpdateCheck::Available;
    const char *state=!v.operation_message.empty()?v.operation_message.c_str():
      !v.wifi_connected?"请先连接 Wi-Fi":!v.platform_available?"更新服务未启用":
      check==SystemPresentation::UpdateCheck::Checking?"正在检查更新":
      check==SystemPresentation::UpdateCheck::Failed?"检查失败，请重试":
      available?"发现新版本":check==SystemPresentation::UpdateCheck::Current?"已是最新版本":"检查是否有新版本";
    bool ok=scene.panel(80,{ui::px(8),layout.top,ui::px(224),ui::px(104)},theme::appsurface);
    ok=pages::icon_badge(scene,81,{ui::px(12),ui::px(12),ui::px(32),ui::px(32)},lunaros::icons::Icon::Download,theme::appaccent,80)&&ok;
    ok=scene.label(20,{ui::px(56),ui::px(12),ui::px(156),ui::px(24)},device_name,ui::font(16),theme::text,graphics::Align::Left,80)&&ok;
    ok=scene.label(21,{ui::px(12),ui::px(48),ui::px(88),ui::px(18)},"当前版本",ui::font(12),theme::appsecondary,graphics::Align::Left,80)&&ok;
    ok=scene.label(22,{ui::px(12),ui::px(68),ui::px(200),ui::px(28)},version,ui::font(28),theme::text,graphics::Align::Left,80)&&ok;
    ok=scene.label(23,{ui::px(12),layout.top+ui::px(112),ui::px(216),ui::px(24)},state,ui::font(14),check==SystemPresentation::UpdateCheck::Failed?theme::warning:theme::appsecondary)&&ok;
    if(available)ok=scene.label(24,{ui::px(12),layout.top+ui::px(140),ui::px(216),ui::px(24)},v.available_version.c_str(),ui::font(20),theme::mint)&&ok;
    return pages::content_actions(scene,layout,{actions.back,"返回"},{actions.confirm,available?"安装更新":"检查更新",v.wifi_connected&&v.platform_available&&check!=SystemPresentation::UpdateCheck::Checking})&&ok;
  }
  if(detail==Detail::Platform) {
    const desktop::ContentLayout layout(auto_hide);
    bool ok=scene.panel(80,{ui::px(8),layout.top,ui::px(224),layout.bottom-layout.top-ui::px(48)},theme::appsurface);
    ok=scene.label(20,{ui::px(12),layout.top+ui::px(8),ui::px(216),ui::px(20)},v.platform_connected?"平台已连接":v.wifi_connected?"平台未连接":"请先连接 Wi-Fi",ui::font(12),theme::appsecondary)&&ok;
    if(v.platform_bound) {
      ok=scene.label(21,{ui::px(12),ui::px(130),ui::px(216),ui::px(28)},v.account_name.empty()?"已配对":v.account_name.c_str(),ui::font(20),theme::text)&&ok;
      ok=scene.label(22,{ui::px(12),ui::px(156),ui::px(216),ui::px(20)},"账号已配对",ui::font(12),theme::success)&&ok;
    } else if(!v.pairing_code.empty()) {
      ok=scene.label(21,{ui::px(12),ui::px(78),ui::px(216),ui::px(40)},v.pairing_code.c_str(),ui::font(28),theme::appaccent)&&ok;
      char expiry[48];std::snprintf(expiry,sizeof(expiry),"%d 分 %02d 秒内有效",v.pairing_seconds/60,v.pairing_seconds%60);
      ok=scene.label(22,{ui::px(12),ui::px(126),ui::px(216),ui::px(24)},expiry,ui::font(16),theme::text)&&ok;
      ok=scene.label(23,{ui::px(12),ui::px(152),ui::px(216),ui::px(20)},"在平台输入此配对码",ui::font(14),theme::appsecondary)&&ok;
    } else {
      const char *message=!v.operation_message.empty()?v.operation_message.c_str():v.pairing_pending?"正在生成配对码":!v.pairing_error.empty()?"生成失败，请重试":"生成配对码连接账号";
      ok=pages::icon_badge(scene,81,{ui::px(96),layout.top+ui::px(42),ui::px(48),ui::px(48)},lunaros::icons::Icon::Link,theme::violet)&&ok;
      ok=scene.label(21,{ui::px(12),layout.top+ui::px(103),ui::px(216),ui::px(48)},message,ui::font(16),theme::text)&&ok;
    }
    return pages::content_actions(scene,layout,{actions.back,"返回"},{v.platform_bound?0:actions.confirm,v.pairing_code.empty()?"生成配对码":"重新生成",v.wifi_connected&&v.platform_available&&!v.pairing_pending})&&ok;
  }
  if(overlay_) {
    if(setup_link_qr_)qr_.update_url(v.setup_url);
    else qr_.update(true,step,v.setup_ssid,v.setup_password,v.setup_ip);
    const int side=kSetupLinkQrSide;
    if(!qr_.qr.ready()||qr_.qr.display_size()*2>side) {
      overlay_=false;
      return pages::message(scene,"配网码暂不可用","请稍后重试",theme::Tone::Warning);
    }
    // The system header stays informational; QR starts below its reserved area.
    return true;
  }
  return pages::message(scene,"配网模式已关闭","返回后可重新开启")&&pages::actions(scene,{actions.back,"返回"});
}
bool DetailRenderer::paint_overlay(void *context,lunaros::QrCode::FillRect fill) const {
  if(!overlay_)return true;
  // The square covers the complete available QR region; no smoothed pixels.
  fill(context,ui::px(20),ui::px(32),kSetupLinkQrSide,kSetupLinkQrSide,false);
  return qr_.paint(ui::px(20),ui::px(32),context,fill,kSetupLinkQrSide);
}
void DetailRenderer::paint_activity(graphics::canvas::Surface surface) const {
  if(avatar_&&avatar_->width&&avatar_->height&&avatar_->pixels.size()==size_t(avatar_->width)*avatar_->height){
    for(int y=0;y<ui::px(56);++y)for(int x=0;x<ui::px(56);++x){
      const float distance=std::hypot(float(x)-ui::px(56)/2.f+.5f,float(y)-ui::px(56)/2.f+.5f);
      const float coverage=surface.antialias?std::clamp(ui::px(56)/2.f-distance,0.f,1.f):distance<ui::px(56)/2.f-.5f?1.f:0.f;
      surface.pixel(ui::px(92)+x,ui::px(67)+y,avatar_->pixels[size_t(y*avatar_->height/ui::px(56))*avatar_->width+x*avatar_->width/ui::px(56)],uint8_t(coverage*255));
    }
  }
  if(!updating_)return;
  constexpr float pi=3.14159265358979323846f;
  for(unsigned i=0;i<12;++i) {
    const float angle=float(i)*pi/6;
    const int cx=ui::px(120)+int(std::sin(angle)*ui::px(25)),cy=ui::px(108)-int(std::cos(angle)*ui::px(25));
    const unsigned age=(activity_phase_+12-i)%12;
    const uint8_t opacity=uint8_t(255-age*17);
    for(int y=-4;y<=4;++y)for(int x=-4;x<=4;++x) {
      const float coverage=std::clamp(4.5f-std::hypot(float(x),float(y)),0.f,1.f);
      if(coverage>0)surface.pixel(cx+x,cy+y,theme::appaccent,uint8_t(coverage*255),opacity);
    }
  }
}
}
