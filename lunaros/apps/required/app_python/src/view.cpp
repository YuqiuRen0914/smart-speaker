#include "lunaros_python_view.h"

namespace lunaredu::python {
bool paint_presentation(lunaros::graphics::Scene &scene,Presentation view,bool hide_status) {
  const desktop::ContentLayout layout(hide_status);
  namespace theme=lunaros::graphics::theme;
  namespace page=lunaros::graphics::page;
  constexpr int padding=ui::px(10),width=desktop::ContentLayout::width-padding*2;
  return scene.panel(100,{layout.inset,layout.top,layout.width,layout.height()-ui::px(48)},theme::appsurface)&&
      scene.flex_layout(100,false,ui::px(6),padding,lunaros::graphics::Align::Center)&&
      scene.flow(110,{0,0,width,ui::px(40)},true,ui::px(8),100)&&
      page::icon_badge(scene,101,{0,0,ui::px(40),ui::px(40)},lunaros::icons::Icon::Code,theme::violet,110)&&scene.flow_item(101)&&
      page::status_badge(scene,104,{0,0,ui::px(92),ui::px(24)},view.stopping?"正在停止":view.running?"正在运行":"设备就绪",view.running?theme::mint:theme::appsecondary,110)&&
      scene.label(1,{0,0,width,ui::px(30)},"编程工作台",ui::font(24),theme::text,lunaros::graphics::Align::Left,100)&&
      scene.label(2,{0,0,width,ui::px(40)},"连接上位机\n编写和运行代码",ui::font(14),theme::appsecondary,lunaros::graphics::Align::Left,100)&&
      lunaros::graphics::page::content_actions(scene,layout,{}, {kExitAction,"返回桌面"});
}
}
