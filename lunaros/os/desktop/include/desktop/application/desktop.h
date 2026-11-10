#pragma once
#include "app/package/application_package.h"
#include "desktop/application/catalog.h"

namespace lunaredu::lunaros::apps {
// Launcher projection depends on the OS installed catalog, never on Store state.
inline std::shared_ptr<const desktop::DesktopApplicationCatalog> desktop_catalog(
    const desktop::DesktopApplication *builtins, size_t count,
    const std::vector<Application> &applications) {
  std::vector<desktop::OwnedDesktopApplication> installed;
  for (const auto &app : applications)
    installed.push_back({app.id, app.name, desktop::DesktopIcon::Games, app.color,
        desktop::DesktopAppKind::Application, {}, app.icon_resource});
  return std::make_shared<const desktop::DesktopApplicationCatalog>(builtins, count, std::move(installed));
}
// Launcher-owner coordination. The caller supplies replacement permission;
// there is no product page, Store state, input policy or new executor here.
class DesktopCatalogController {
 public:
  DesktopCatalogController(const desktop::DesktopApplication *builtins, size_t count)
      : builtins_(builtins), count_(count) {}
  bool dirty() const { return dirty_; }
  void invalidate() { dirty_ = true; }
  const std::shared_ptr<const desktop::DesktopApplicationCatalog> &snapshot() const { return catalog_; }
  void observe(const std::vector<Application> &installed) {
    if (installed.size() != installed_.size()) { invalidate(); return; }
    for (size_t i = 0; i < installed.size(); ++i) {
      const auto &a = installed[i]; const auto &b = installed_[i];
      if (a.id != b.id || a.name != b.name || a.version != b.version ||
          a.color != b.color || a.icon_resource != b.icon) { invalidate(); return; }
    }
  }
  template<class Publish> bool apply(const std::vector<Application> &installed,
             bool replacement_allowed,Publish publish) {
    if (!dirty_ || !replacement_allowed) return false;
    auto next = desktop_catalog(builtins_, count_, installed);
    if (!publish(next)) return false;
    catalog_ = std::move(next);
    installed_.clear(); installed_.reserve(installed.size());
    for (const auto &a : installed) installed_.push_back({a.id, a.name, a.version, a.icon_resource, a.color});
    dirty_ = false;
    return true;
  }
 private:
  const desktop::DesktopApplication *builtins_;
  size_t count_;
  bool dirty_ = true;
  // Retain only launcher identity, not package bytes, descriptions or URLs.
  struct Identity { std::string id, name, version, icon; uint16_t color; };
  std::vector<Identity> installed_;
  std::shared_ptr<const desktop::DesktopApplicationCatalog> catalog_;
};
}
