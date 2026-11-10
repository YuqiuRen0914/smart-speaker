#include "desktop/ui/system_canvas.h"
namespace lunaredu::desktop::system_canvas {
bool startup(Scene &scene,const char *product) {
  return scene.panel(0xfffc0000,{0,0,ui::side,ui::side},theme::background,0,0)&&
    scene.label(0xfffc0001,{0,ui::px(56),ui::px(240),ui::px(48)},"LUNAR",type::splash.size,theme::accent)&&
    scene.label(0xfffc0002,{ui::px(12),ui::px(114),ui::px(216),ui::px(32)},product,type::headline.size,theme::text)&&
    scene.spinner(0xfffc0003,{ui::px(108),ui::px(160),ui::px(24),ui::px(24)},theme::accent);
}
}
