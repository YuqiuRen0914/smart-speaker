#include "desktop/navigation/snapshot.h"
#include "desktop/graphics/canvas_spec.h"
#include "desktop/navigation/home_navigation_renderer.h"
#include "desktop/navigation/compositor.h"
#include "navigation/edge_navigation_renderer.h"
#include <algorithm>

namespace lunaredu::desktop {
DesktopNavigationCompositor::~DesktopNavigationCompositor(){retire_navigation_snapshots();}
bool DesktopNavigationCompositor::compose(const desktop::DesktopShell &shell,
    const desktop::DesktopStatus &status, desktop::DesktopPainter painter,
    uint16_t *frame, unsigned rotation, bool overlays_allowed,lunaros::graphics::Scene *renderer,bool *failed) {
  if(failed)*failed=false;
  bool full = false;
  const auto &nav = shell.home_navigation;
  if (nav.enabled && overlays_allowed) {
    if (nav.progress > 0 && source_ && background_) {
      if (capture_epoch_ != nav.epoch) {
        std::copy_n(frame, kDesktopCanvas.pixels(), source_);
        NavigationSnapshot snapshot(shell);
        if(!snapshot){if(failed)*failed=true;return false;}
        auto &background = *snapshot;
        background.surface = desktop::DesktopSurface::Launcher;
        background.home_navigation.reset(); background.settle_motion();
        const auto tile = background.tile(background.focus);
        float x = tile.x, y = tile.y;
        for (unsigned r = 0; r < background.preferences.rotation; ++r) {
          const float previous = x; x = canvas_layout::last - y; y = previous;
        }
        for (unsigned r = 0; r < rotation; ++r) {
          const float previous = x; x = y; y = canvas_layout::last - previous;
        }
        target_ = {x, y, tile.diameter};
        if(!desktop::draw_launcher(background, status, background_, painter, false,nullptr,renderer)){
          if(failed)*failed=true;
          capture_epoch_=UINT32_MAX;return false;
        }
        capture_epoch_ = nav.epoch;
      }
      std::copy_n(background_, kDesktopCanvas.pixels(), frame);
      if(!desktop::compose_home_card(frame, source_, nav, target_, rotation, shell.preferences.antialias,renderer)){
        if(failed)*failed=true;
        return false;
      }
      full = true;
    } else capture_epoch_ = UINT32_MAX;
    if(!desktop::draw_home_indicator(nav, frame, rotation, shell.preferences.antialias,renderer)){
      if(failed)*failed=true;
      return false;
    }
  } else capture_epoch_ = UINT32_MAX;
  if(overlays_allowed&&!desktop::draw_edge_back_indicator(shell.edge_navigation,frame,rotation,shell.preferences.antialias,renderer)){
    if(failed)*failed=true;
    return false;
  }
  return full;
}
} // namespace lunaredu::desktop
