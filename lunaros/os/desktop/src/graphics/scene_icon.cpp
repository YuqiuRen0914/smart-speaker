#include "desktop/graphics/scene_icon.h"
namespace lunaredu::desktop {

bool scene_icon(lunaros::graphics::Scene &scene,uint32_t id,lunaros::graphics::Rect box,
    lunaros::icons::Icon icon,uint16_t color,uint32_t parent,bool antialias) {
  if(box.width<=0||box.height<=0||box.width>lunaros::graphics::metrics::px(38)||
      box.height>lunaros::graphics::metrics::px(38))return false;
  // Immutable source crop, no intermediate fitted raster or global cache.
  return scene.mask(id,box,lunaros::icons::builtin(icon),lunaros::icons::bounds(icon),color,
                    1+uint64_t(unsigned(icon)),parent,antialias);
}
}
