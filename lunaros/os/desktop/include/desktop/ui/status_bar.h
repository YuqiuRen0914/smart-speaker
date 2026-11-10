#pragma once
#include "desktop/shell/shell.h"
#include "graphics/canvas_spec.h"
#include "lunaros_ui_status_layout.generated.h"
namespace lunaredu::lunaros::graphics {class Scene;class CanvasPainter;}

namespace lunaredu::desktop {
bool status_overlay_shows_bar(const DesktopStatus &,const StatusNavigation &,bool automatic);
// Scene consumer; direct canvas/Pet callers keep their borrowed-pixel contract.
bool build_status_overlay(lunaros::graphics::Scene &,const char *title,const DesktopStatus &,
                          const StatusNavigation &,bool automatic,DesktopPainter,bool antialias);
// Same semantic projection on a render-owner borrowed Canvas.
bool build_status_overlay(lunaros::graphics::CanvasPainter &,const char *title,const DesktopStatus &,
                          const StatusNavigation &,bool automatic,const char *detail=nullptr);
// Same-owner RGB565 raw App chrome, including bounded partial-height targets.
bool paint_status_overlay(lunaros::graphics::Scene &,lunaros::graphics::CanvasSpec,uint16_t *,
                          const char *,const DesktopStatus &,const StatusNavigation &,
                          bool automatic,bool antialias,const char *detail=nullptr);
static_assert(status_layout::width==kDesktopCanvas.width,"Status layout must match the Desktop canvas");
}
