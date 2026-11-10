#include "lunaros_settings_bluetooth.h"
#include "desktop/ui/content_layout.h"
#include "desktop/graphics/page.h"
namespace lunaredu::settings {
bool build_bluetooth_surface(lunaros::graphics::Scene &scene,const BluetoothPresentation &v,unsigned scroll,bool auto_hide,unsigned focus,bool busy){
  namespace ui=lunaros::graphics::metrics;
  namespace theme=lunaros::graphics::theme;
  namespace pages=lunaros::graphics::page;
  const desktop::ContentLayout layout(auto_hide);
  const int left=ui::px(8),width=ui::px(224),gap=ui::gap,h=ui::min_touch;
  const int half=(width-gap)/2,header=ui::line_height(12,24);
  const size_t count=std::min(v.device_count,v.devices.size());
  bool ok=scene.container(250,{left,layout.top,width,layout.bottom-layout.top},int(std::min(scroll,unsigned(INT32_MAX))),269);
  ok=scene.flex_layout(250,false,gap)&&ok;
  unsigned order=0;
  ok=pages::switch_row(scene,10,110,{0,0,width,h},"蓝牙",v.enabled,focus==0,v.available&&!busy,250)&&ok;
  ok=scene.flow_item(10,0,order++)&&ok;
  const auto button=[&](uint32_t id,int w,const char *label,bool enabled,uint32_t parent){
    const unsigned index=id==260?1:(id==261||id==263)?2:3;
    ok=scene.button(id,{0,0,w,h},label,ui::font(14),theme::raised,theme::text,focus==index,enabled&&!busy,parent)&&ok;
    ok=scene.flow_item(id)&&ok;
  };
  ok=scene.label(251,{0,0,width,header},v.confirm_pending?"核对手机上的配对码":v.connected?"设备已连接":v.pairing?"配对窗口已开启 · 60 秒":"尚未连接设备",ui::font(12),theme::secondary,lunaros::graphics::Align::Center,250)&&ok;
  ok=scene.flow_item(251,0,order++)&&ok;
  if(v.pairing_supported){button(260,width,v.pairing?"重新开启配对窗口":"允许手机配对",v.available&&v.enabled,250);ok=scene.flow_item(260,0,order++)&&ok;}
  if(v.confirm_pending){
    char code[32];std::snprintf(code,sizeof(code),"%06u",unsigned(v.pairing_code));
    ok=scene.label(252,{0,0,width,h},code,ui::font(28),theme::text,lunaros::graphics::Align::Center,250)&&scene.flow_item(252,0,order++)&&ok;
    ok=scene.flow(270,{0,0,width,h},true,gap,250)&&scene.flow_item(270,0,order++)&&ok;
    button(263,half,"确认相同",v.enabled,270);
    button(264,half,"拒绝配对",v.enabled,270);
  }else{
    if(v.ble_scan_supported||v.classic_supported){
      ok=scene.flow(271,{0,0,width,h},true,gap,250)&&scene.flow_item(271,0,order++)&&ok;
      if(v.ble_scan_supported)button(261,v.classic_supported?half:width,"扫描 BLE",v.available&&v.enabled&&!v.discovering,271);
      if(v.classic_supported)button(262,v.ble_scan_supported?half:width,"扫描经典蓝牙",v.available&&v.enabled&&!v.discovering,271);
    }
    const char *hint=busy?"正在处理蓝牙设置":v.error[0]?"操作未完成，请重试":!v.enabled?"开启蓝牙后可使用设备支持的功能":v.discovering?"正在寻找附近设备…":v.pairing_supported?"在手机上选择 LunarOS 发起配对":"当前设备支持 BLE";
    ok=scene.label(253,{0,0,width,h},hint,ui::font(12),v.error[0]?theme::warning:theme::secondary,lunaros::graphics::Align::Center,250)&&scene.flow_item(253,0,order++)&&ok;
    for(size_t i=0;i<count;++i){
      const auto &d=v.devices[i];const uint32_t id=3000+uint32_t(i)*3;
      ok=scene.panel(id,{0,0,width,h},theme::appsurface,250)&&scene.flow_item(id,0,order++)&&ok;
      ok=scene.flex_layout(id,false,0,gap,lunaros::graphics::Align::Center)&&ok;
      ok=scene.label(id+1,{0,0,width-gap*2,h/2},d.name[0]?d.name.data():"未命名设备",ui::font(14),theme::text,lunaros::graphics::Align::Left,id,true)&&ok;
      char detail[48];std::snprintf(detail,sizeof(detail),"%s · %d dBm",d.classic?"经典蓝牙":"BLE",int(d.rssi));
      ok=scene.label(id+2,{0,0,width-gap*2,h/2},detail,ui::font(10),theme::secondary,lunaros::graphics::Align::Left,id)&&ok;
    }
    if(!count)ok=scene.label(254,{0,0,width,h},v.discovering?"扫描中":"暂无附近设备",ui::font(12),theme::secondary,lunaros::graphics::Align::Center,250)&&scene.flow_item(254,0,order++)&&ok;
  }
  return ok;
}
}
