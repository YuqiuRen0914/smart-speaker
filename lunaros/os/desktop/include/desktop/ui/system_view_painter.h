#pragma once
#include "desktop/ui/screen_paint_port.h"
#include "desktop/ui/system_canvas.h"
#include "desktop/graphics/page.h"
#include <array>
namespace lunaredu::desktop {
void paint_unavailable(const char *title,const char *reason,ScreenPaintPort &port);
void paint_diagnostic(bool orientation,ScreenPaintPort &port);
void paint_startup(const char *device_name,ScreenPaintPort &port,uint32_t elapsed=0);
}
