#include "lunaros_app_manager_view.h"
#include <algorithm>
#include <cstdio>
namespace lunaredu::lunaros::apps::manager {

void application_icon(graphics::Scene &scene,uint32_t id,graphics::Rect box,
    const Application &app,uint32_t parent) {
  namespace ui=graphics::metrics;
  scene.panel(id,box,app.color,parent,ui::px(7));
  const int size=box.width-ui::px(10);
  desktop::scene_icon(scene,id+1,{ui::px(5),ui::px(5),size,size},icons::find(app.icon_resource.c_str()),graphics::theme::text,id);
}

void paint(graphics::Scene &scene,const View &v,const StoreSnapshot &s,const CatalogLayout &layout) {
  namespace page=graphics::page;namespace theme=graphics::theme;
  namespace ui=graphics::metrics;
  using graphics::Align;
  constexpr uint32_t body=0xffb10000,header=body+1,heading=body+2,controls=body+3,empty=body+4,
    detail_header=body+5,emblem=body+6;
  const bool working=busy(s),loading=s.phase==StorePhase::Fetching;
  if(!v.detail_id.empty()) {
    const auto app=find(s.discovered.entries(),v.detail_id);
    if(!app){
      page::content_message(scene,layout,"应用已不在商店中","返回商店查看其他应用");
      page::content_actions(scene,layout,{Cancel,"返回商店"});return;
    }
    scene.flow(body,{layout.inset,layout.top,layout.width,layout.height()-ui::px(48)},false,ui::px(4));
    scene.flow(detail_header,{0,0,layout.width,ui::px(44)},true,ui::px(10),body);scene.flow_item(detail_header,0,0);
    application_icon(scene,1200,{0,0,ui::px(44),ui::px(44)},*app,detail_header);
    scene.flow(heading,{0,0,ui::px(170),ui::px(44)},false,ui::px(2),detail_header);scene.flow_item(heading,1);
    scene.label(1100,{0,0,ui::px(170),ui::px(24)},app->name.c_str(),ui::font(20),theme::text,Align::Left,heading);
    const auto metadata="v"+app->version+" · "+package_size(app->size);
    scene.label(1101,{0,0,ui::px(170),ui::px(18)},metadata.c_str(),ui::font(12),theme::appsecondary,Align::Left,heading);
    const int description_height=layout.height()-ui::px(144);
    scene.panel(1106,{0,0,layout.width,description_height},theme::appsurface,body);scene.flow_item(1106,1,1);
    scene.flex_layout(1106,false,0);
    scene.container(1102,{0,0,layout.width,description_height},0,0,false,1106);scene.flow_item(1102,1);
    scene.paragraph(1103,{ui::px(12),ui::px(10),layout.width-ui::px(24),description_height},app->description.empty()?"这款应用暂未提供介绍。":app->description.c_str(),ui::font(16),theme::text,1102);
    const bool installed=find(s.installed.entries(),app->id),newer=update_available(s,app->id);
    std::string message=working||loading?status(s):v.failed?"安装未完成，请重试":
      !s.error.empty()?(!s.message.empty()?s.message:status(s)):!v.message.empty()?v.message:
      installed&&!newer&&!s.message.empty()?s.message:newer?"有新版本，可以更新":installed?"已安装，从桌面打开":"安装后会出现在桌面";
    if(s.phase==StorePhase::Downloading){
      message=s.total_bytes?"正在下载 · "+std::to_string(download_percent(s))+"%":"正在下载 · "+package_size(s.received_bytes);
    }
    scene.label(1104,{0,0,layout.width,ui::px(32)},message.c_str(),ui::font(14),v.failed||!s.error.empty()?theme::danger:working?theme::appaccent:theme::text,Align::Center,body);scene.flow_item(1104,0,2);
    if(working&&(s.total_bytes||s.phase!=StorePhase::Downloading)){
      scene.progress(1105,{0,0,layout.width,ui::px(6)},s.phase==StorePhase::Downloading?download_percent(s):100,theme::appaccent,body);scene.flow_item(1105,0,3);
    }
    page::content_actions(scene,layout,{Cancel,"返回商店"},{Primary,working?"正在安装":loading?"正在刷新":newer?"更新应用":installed?"已安装":"安装",!working&&!loading&&(!installed||newer)});
    return;
  }
  scene.flow(header,{layout.inset,layout.top,layout.width,ui::px(32)},true,ui::px(8));
  desktop::scene_icon(scene,1000,{0,0,ui::px(24),ui::px(24)},icons::Icon::Store,theme::appaccent,header);
  scene.label(1001,{0,0,ui::px(64),ui::px(32)},"商店",ui::font(20),theme::text,Align::Left,header);
  scene.flow(controls,{0,0,ui::px(120),ui::px(32)},true,ui::px(4),header);scene.flow_item(controls,1);
  scene.flex_layout(controls,true,ui::px(4),0,Align::Right);
  const size_t count=s.discovered.size(),selection=count?std::min(v.selection,count-1):0,begin=selection/3*3;
  if(!count){
    scene.flow(empty,{layout.inset,layout.first,layout.width,layout.bottom-ui::px(48)-layout.first},false,ui::px(8));
    scene.flex_layout(empty,false,ui::px(8),0,Align::Center);
    scene.flow(emblem,{0,0,layout.width,ui::px(38)},true,0,empty);scene.flex_layout(emblem,true,0,0,Align::Center);
    desktop::scene_icon(scene,1300,{0,0,ui::px(38),ui::px(38)},icons::Icon::Store,theme::appsecondary,emblem);
    scene.label(1301,{0,0,layout.width,ui::px(28)},loading?"正在连接商店":!s.error.empty()?"暂时无法连接商店":"暂无可安装的应用",ui::font(18),theme::text,Align::Center,empty);
    scene.label(1302,{0,0,layout.width,ui::px(22)},loading?"正在读取应用目录":!s.error.empty()?"检查网络后再试一次":"稍后刷新看看",ui::font(14),theme::appsecondary,Align::Center,empty);
    page::content_actions(scene,layout,{},{Refresh,loading?"正在加载":"刷新商店",!loading});return;
  }
  if(!s.error.empty()||v.failed){
    // Separate node IDs keep the header retry and empty-state footer stable.
    scene.button(Retry,{0,0,ui::px(64),ui::px(32)},"重试",ui::font(14),theme::appsurface,theme::appaccent,false,!working&&!loading,controls);
  }else if(v.pull_distance>0||working||loading){
    std::string text=v.pull_distance>0?(v.pull_distance>=48?"松开刷新":"下拉刷新"):loading?"正在刷新":s.phase==StorePhase::Downloading?
      (s.total_bytes?"下载 "+std::to_string(download_percent(s))+"%":"正在下载"):s.phase==StorePhase::Verifying?"正在校验":"正在安装";
    scene.label(1002,{0,0,ui::px(120),ui::px(32)},text.c_str(),ui::font(14),theme::appaccent,Align::Right,controls);scene.flow_item(1002,1);
  }else{
    char pages[24];std::snprintf(pages,sizeof(pages),"%u/%u",unsigned(begin/3+1),unsigned((count+2)/3));
    scene.button(Previous,{0,0,ui::px(32),ui::px(32)},"",ui::font(14),theme::appsurface,theme::text,false,begin>0,controls);scene.flow_item(Previous,0,0);
    desktop::scene_icon(scene,1004,{ui::px(8),ui::px(8),ui::px(16),ui::px(16)},icons::Icon::ArrowLeft,theme::text,Previous);
    scene.label(1003,{0,0,ui::px(44),ui::px(32)},pages,ui::font(14),theme::appsecondary,Align::Center,controls);scene.flow_item(1003,0,1);
    scene.button(Next,{0,0,ui::px(32),ui::px(32)},"",ui::font(14),theme::appsurface,theme::text,false,begin+3<count,controls);scene.flow_item(Next,0,2);
    desktop::scene_icon(scene,1005,{ui::px(8),ui::px(8),ui::px(16),ui::px(16)},icons::Icon::ArrowRight,theme::text,Next);
  }
  scene.flow(body,{layout.inset,layout.first,layout.width,layout.bottom-layout.first},false,layout.gap);
  for(size_t row=0;row<3&&begin+row<count;++row) {
    const auto &app=s.discovered[begin+row];const uint32_t id=Row+begin+row,child=2000+unsigned(begin+row)*8;
    const bool installed=find(s.installed.entries(),app.id),newer=update_available(s,app.id);
    scene.button(id,{0,0,layout.width,layout.item_height},"",ui::font(16),theme::appsurface,theme::text,begin+row==selection,!working,body);scene.flow_item(id,0,unsigned(row));
    scene.flex_layout(id,true,ui::px(8),ui::px(8));
    application_icon(scene,child,{0,0,ui::px(34),ui::px(34)},app,id);
    scene.flow(child+6,{0,0,ui::px(114),ui::px(38)},false,ui::px(2),id);scene.flow_item(child+6,1);
    scene.label(child+2,{0,0,ui::px(114),ui::px(22)},app.name.c_str(),ui::font(16),theme::text,Align::Left,child+6);
    const auto metadata="v"+app.version+" · "+package_size(app.size);
    scene.label(child+3,{0,0,ui::px(114),ui::px(14)},metadata.c_str(),ui::font(12),theme::appsecondary,Align::Left,child+6);
    scene.panel(child+5,{0,0,ui::px(44),ui::px(26)},newer||!installed?theme::appaccent:theme::appraised,id,ui::px(7));
    scene.label(child+4,{0,0,ui::px(44),ui::px(26)},newer?"更新":installed?"已装":"获取",ui::font(14),newer||!installed?theme::background:theme::appsecondary,Align::Center,child+5);
  }
}
}
