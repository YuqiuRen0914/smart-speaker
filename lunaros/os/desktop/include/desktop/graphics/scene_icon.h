#pragma once
#include "graphics/scene.h"
#include "graphics/icon_draw.h"
#include "lunaros_ui_metrics.h"
namespace lunaredu::desktop {
bool scene_icon(lunaros::graphics::Scene &scene,uint32_t id,lunaros::graphics::Rect box,
    lunaros::icons::Icon icon,uint16_t color,uint32_t parent=0,bool antialias=true);
}
