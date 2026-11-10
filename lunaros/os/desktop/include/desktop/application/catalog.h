#pragma once
#include "desktop/shell/shell.h"
#include <string>
#include <vector>
namespace lunaredu::desktop {
struct OwnedDesktopApplication {
  std::string id, name;
  DesktopIcon icon = DesktopIcon::Games;
  uint16_t color = 0xfdcd;
  DesktopAppKind kind = DesktopAppKind::Application;
  ApplicationRenderHints rendering{};
  std::string icon_resource{};
  DesktopAppCategory category = DesktopAppCategory::Extension;
  bool uninstallable = true;
};
// Immutable storage retained alongside render snapshots. Installing an app
// never changes the boot-sealed hardware capability registry.
class DesktopApplicationCatalog {
public:
  DesktopApplicationCatalog(const DesktopApplication *builtins, size_t count,
                            std::vector<OwnedDesktopApplication> installed);
  const DesktopApplication *data() const { return declarations_.data(); }
  size_t size() const { return declarations_.size(); }

private:
  std::vector<OwnedDesktopApplication> owned_;
  std::vector<DesktopApplication> declarations_;
};
} // namespace lunaredu::desktop
