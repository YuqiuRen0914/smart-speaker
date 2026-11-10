#pragma once
#include <cstdint>
#include "graphics/render_policy.h"

namespace lunaredu::desktop {
enum class AntialiasRequest : uint8_t { Inherit, Disabled };
// App declarations describe their own content, never system chrome. Image
// interpolation is independent of edge AA; pixel art always uses nearest.
struct ApplicationRenderHints {
  AntialiasRequest antialias = AntialiasRequest::Inherit;
  ::lunaredu::lunaros::raster::ImageSampling images = ::lunaredu::lunaros::raster::ImageSampling::Nearest;
};

constexpr bool valid_render_hints(ApplicationRenderHints hints) {
  return static_cast<unsigned>(hints.antialias) <= 1 &&
         static_cast<unsigned>(hints.images) <= 1;
}
constexpr ::lunaredu::lunaros::raster::RenderPolicy resolve_render_policy(bool system_antialias,
    ApplicationRenderHints hints = {}, bool system_surface = false) {
  return {system_antialias && (system_surface ||
      hints.antialias == AntialiasRequest::Inherit),
      system_surface ? ::lunaredu::lunaros::raster::ImageSampling::Nearest : hints.images};
}
} // namespace lunaredu::desktop
