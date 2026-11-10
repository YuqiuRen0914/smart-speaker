#pragma once
#include "desktop/shell/shell.h"
namespace lunaredu::desktop {
// Two renderer snapshots, with nonblocking cold fallback for nested callers.
// Return drops all application/image leases; retirement never invalidates a loan.
class NavigationSnapshot {
 public:
  explicit NavigationSnapshot(const DesktopShell &);
  ~NavigationSnapshot();
  NavigationSnapshot(const NavigationSnapshot &)=delete;
  NavigationSnapshot &operator=(const NavigationSnapshot &)=delete;
  DesktopShell *operator->() const { return shell_; }
  DesktopShell &operator*() const { return *shell_; }
  explicit operator bool() const { return shell_!=nullptr; }
 private:
  DesktopShell *shell_=nullptr;
  int slot_=-1;
};
void retire_navigation_snapshots();
void release_shell_render_leases(DesktopShell &);
}
