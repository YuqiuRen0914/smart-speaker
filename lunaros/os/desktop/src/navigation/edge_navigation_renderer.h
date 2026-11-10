#pragma once
#include "desktop/navigation/edge_navigation.h"
namespace lunaredu::lunaros::graphics {class Scene;}
namespace lunaredu::desktop {
// Product Bezier profile, rendered by the existing synchronous LVGL owner.
// False rejects this output frame; no private pixel fallback is retained.
bool draw_edge_back_indicator(const EdgeNavigation &,uint16_t *,unsigned rotation=0,
                              bool antialias=true,lunaros::graphics::Scene *renderer=nullptr);
}
