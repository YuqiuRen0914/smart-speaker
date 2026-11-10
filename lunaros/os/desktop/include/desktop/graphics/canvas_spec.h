#pragma once
#include "graphics/canvas_spec.h"
#include "lunaros_ui_metrics.h"

namespace lunaredu::desktop {
// System chrome currently has one logical layout; physical panels may map it
// through CanvasViewport. This is independent of generic Display dimensions.
inline constexpr lunaros::graphics::CanvasSpec kDesktopCanvas{
    lunaros::graphics::metrics::side,lunaros::graphics::metrics::side,lunaros::graphics::metrics::side};
static_assert(kDesktopCanvas.width == kDesktopCanvas.height, "Desktop uses a square logical layout");
namespace canvas_layout {
inline constexpr int width=kDesktopCanvas.width, height=kDesktopCanvas.height;
inline constexpr int side=width, last=side-1;
inline constexpr float center=side/2.f;
}
struct DesktopRenderOptions {
  bool home_card_animation = true;
  bool default_grid = false;
  uint8_t grid_columns = 2, grid_rows = 2;
  bool phone_desktop = false;
  constexpr unsigned auxiliary_frames() const { return home_card_animation ? 2 : 0; }
};
}
