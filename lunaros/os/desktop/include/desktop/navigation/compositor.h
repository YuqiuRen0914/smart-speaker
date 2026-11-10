#pragma once
#include "desktop/navigation/home_navigation_renderer.h"
#include "desktop/shell/shell.h"

namespace lunaredu::desktop {
// Renderer-owned system navigation composition. Canvases belong to the resident
// pipeline; this object only remembers the current navigation capture.
class DesktopNavigationCompositor {
 public:
  ~DesktopNavigationCompositor();
  DesktopNavigationCompositor()=default;
  DesktopNavigationCompositor(const DesktopNavigationCompositor &)=delete;
  DesktopNavigationCompositor &operator=(const DesktopNavigationCompositor &)=delete;
  void bind(uint16_t *source, uint16_t *background) {
    source_ = source; background_ = background;
  }
  bool captured(uint32_t epoch) const { return capture_epoch_ == epoch; }
  // Returns true when the navigation card expanded a patch to the full frame.
  bool compose(const desktop::DesktopShell &, const desktop::DesktopStatus &,
               desktop::DesktopPainter, uint16_t *frame, unsigned rotation,
               bool overlays_allowed,lunaros::graphics::Scene *renderer=nullptr,
               bool *failed=nullptr);
 private:
  uint16_t *source_ = nullptr, *background_ = nullptr;
  desktop::HomeTarget target_{};
  uint32_t capture_epoch_ = UINT32_MAX;
};
} // namespace lunaredu::desktop
