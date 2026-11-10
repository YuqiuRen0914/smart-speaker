#include "lunaros_ui_metrics.h"
#pragma once
#include "desktop/graphics/canvas.h"
#include "graphics/scene.h"
namespace lunaredu::desktop::system_canvas {
namespace ui=::lunaredu::lunaros::graphics::metrics;
using namespace lunaros::graphics;
namespace type=typography;
inline constexpr uint32_t startup_duration_ms=800;
bool startup(Scene &scene,const char *product);
enum class Diagnostic { Bars, Orientation };
void diagnostic(canvas::Surface s,Diagnostic pattern);
}
