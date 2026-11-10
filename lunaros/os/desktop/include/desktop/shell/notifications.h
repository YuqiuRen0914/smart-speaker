#pragma once
#include "desktop/shell/shell.h"
#include "system/notifications.h"
namespace lunaredu::desktop::notifications {
struct State : DesktopSurfaceState {
  State();
  lunaros::notifications::Snapshot notices;
};
const State *state(const DesktopShell &);
bool refresh(DesktopShell &);
const DesktopSurfaceExtension &extension();
// Settings installs this child surface. It never registers another App.
bool install_surface(DesktopShell &,uint64_t (*clock_ms)() = nullptr);
bool remove_surface(DesktopShell &);
}
