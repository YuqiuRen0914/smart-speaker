#pragma once
#include "desktop/composition/selection.h"
#include "desktop/ui/paint_port.h"

namespace lunaredu::lunaros {
// Native frame owners use the same foreground admission and route projection
// as widget Apps. This entry paints no business content and owns no driver.
struct NativeDesktopPresentation {
  void paint(desktop::UiPaintPort &, bool) const {}
};
template<class Description> struct NativeDesktopEntry : DesktopEntryDefaults {
  using Presentation = NativeDesktopPresentation;
  template<class Bindings> static typename Bindings::Definition binding() {
    return {Description::id,
      [](typename Bindings::CaptureContext &) -> typename Bindings::Presentation {
        return NativeDesktopPresentation{};
      },
      [](typename Bindings::InputContext &) { return false; },
      [](typename Bindings::EntryContext &c) { c.enter_ui({Description::route()}); },
      [](typename Bindings::LaunchContext &c, std::string &error) {
        return c.host.applications().switch_to(Description::id, error) &&
               c.enter_ui({Description::route()});
      }};
  }
};
}
