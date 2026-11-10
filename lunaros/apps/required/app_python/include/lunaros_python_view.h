#include "lunaros_ui_metrics.h"
#pragma once
#include "desktop/graphics/page.h"
namespace lunaredu::python {
namespace ui=::lunaredu::lunaros::graphics::metrics;
struct Presentation {
  bool running = false, stopping = false;
  bool operator==(const Presentation &other) const {return running==other.running&&stopping==other.stopping;}
  bool operator!=(const Presentation &other) const {return !(*this==other);}
};
Presentation presentation();
inline constexpr uint32_t kExitAction=7100;
bool paint_presentation(lunaros::graphics::Scene &scene,Presentation view,bool hide_status=false);
}
