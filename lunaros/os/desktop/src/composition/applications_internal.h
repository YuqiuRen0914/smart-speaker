#pragma once
#include "desktop/composition/applications.h"
namespace lunaredu::desktop { class DesktopUiHost; struct UiHostState; }
namespace lunaredu::lunaros {
// Composition borrows state within this component; no mutable product port.
struct DesktopCompositionAccess {
  static desktop::UiHostState &state(desktop::DesktopUiHost &);
};
const DesktopAppDescriptor *find_desktop_application(const DesktopAppDescriptor *,size_t,const char *);
}
