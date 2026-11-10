#include "desktop/ui/system_view_painter.h"

namespace lunaredu::desktop {
void paint_unavailable(const char *title,const char *reason,ScreenPaintPort &port) {
  if (!port.begin_widgets()) return;
  const std::array fields{lunaros::graphics::page::Detail{"使用条件",reason}};
  if (lunaros::graphics::page::information(port.graphics().scene(),fields,{2,"返回"}) &&
      port.finish_widgets(title)) port.present_widgets();
}

void paint_diagnostic(bool orientation,ScreenPaintPort &port) {
  if (!port.begin_canvas()) return;
  system_canvas::diagnostic(port.surface(),orientation?
      system_canvas::Diagnostic::Orientation:system_canvas::Diagnostic::Bars);
  port.flush();
}

void paint_startup(const char *device_name,ScreenPaintPort &port,uint32_t elapsed) {
  (void)elapsed; // The session owns the startup deadline; LVGL owns spinner time.
  if(!port.begin_widgets())return;
  if(system_canvas::startup(port.graphics().scene(),device_name)&&port.finish_widgets(nullptr,false))port.present_widgets();
}
}
