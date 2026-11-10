#include "desktop/application/catalog.h"
#include <utility>

namespace lunaredu::desktop {
DesktopApplicationCatalog::DesktopApplicationCatalog(const DesktopApplication *builtins, size_t count,
    std::vector<OwnedDesktopApplication> installed) : owned_(std::move(installed)) {
    owned_.reserve(owned_.size() + count);
    for (size_t i = 0; i < count; i++) {
      const auto &a = builtins[i];
      owned_.insert(owned_.begin() + i,
                    {a.id, a.name, a.icon, a.color, a.kind, a.rendering, a.icon_resource?a.icon_resource:"",a.category,a.uninstallable});
    }
    declarations_.reserve(owned_.size());
    for (const auto &a : owned_)
      declarations_.push_back(
          {a.id.c_str(), a.name.c_str(), a.icon, a.color, a.kind, a.rendering, a.icon_resource.c_str(),a.category,a.uninstallable});
  }
} // namespace lunaredu::desktop
