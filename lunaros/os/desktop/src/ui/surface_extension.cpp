#include "desktop/ui/surface_extension.h"
#include "desktop/shell/shell.h"
#include <array>
#include <cstring>
namespace lunaredu::desktop {
struct DesktopSurfaceRegistry {
  struct Binding {
    const DesktopSurfaceExtension *extension = nullptr;
    std::shared_ptr<const void> context;
  };
  std::array<Binding, static_cast<size_t>(DesktopSurface::Count)> surfaces{};
};
namespace { const DesktopSurfaceExtension empty{}; }
bool install_desktop_surface(DesktopShell &shell, DesktopSurface surface,
    const DesktopSurfaceExtension &extension, std::shared_ptr<const void> context) {
  const auto index=static_cast<unsigned>(surface);
  if(index>=static_cast<unsigned>(DesktopSurface::Count) || !extension.open || !extension.command ||
     static_cast<unsigned>(extension.row_activation)>static_cast<unsigned>(DesktopRowActivation::Disabled) ||
     (extension.system_menu_order>=0 && (surface==DesktopSurface::Launcher || surface==DesktopSurface::System || surface==DesktopSurface::Actions))) return false;
  if(shell.surface_registry) {
    const auto &surfaces=shell.surface_registry->surfaces;
    if(surfaces[index].extension)
      return surfaces[index].extension==&extension && surfaces[index].context==context;
    if(extension.application) {
    const auto *id=extension.application->id;
    if(!id || !*id)return false;
    for(const auto &binding:surfaces)
      if(binding.extension && binding.extension->application &&
          !std::strcmp(binding.extension->application->id,id))return false;
    }
  } else if(extension.application && (!extension.application->id || !*extension.application->id))return false;
  auto next=shell.surface_registry?std::make_shared<DesktopSurfaceRegistry>(*shell.surface_registry):std::make_shared<DesktopSurfaceRegistry>();
  next->surfaces[index]={&extension,std::move(context)};
  shell.surface_registry=std::move(next);++shell.surface_generation;return true;
}
const DesktopSurfaceExtension &desktop_surface_extension(const DesktopShell &shell, DesktopSurface surface) {
  const auto index=static_cast<unsigned>(surface);
  if(!shell.surface_registry || index>=static_cast<unsigned>(DesktopSurface::Count))return empty;
  const auto *extension=shell.surface_registry->surfaces[index].extension;
  return extension?*extension:empty;
}
std::shared_ptr<const void> desktop_surface_context(const DesktopShell &shell,DesktopSurface surface,
    const DesktopSurfaceExtension &expected) {
  if(!shell.surface_registry || static_cast<unsigned>(surface)>=static_cast<unsigned>(DesktopSurface::Count))return nullptr;
  return &desktop_surface_extension(shell,surface)==&expected
      ?shell.surface_registry->surfaces[static_cast<unsigned>(surface)].context:nullptr;
}
bool remove_desktop_surface(DesktopShell &shell, DesktopSurface surface,
    const DesktopSurfaceExtension &extension,const std::shared_ptr<const void> &context) {
  if(!shell.surface_registry || static_cast<unsigned>(surface)>=static_cast<unsigned>(DesktopSurface::Count))return false;
  if(&desktop_surface_extension(shell,surface)!=&extension)return false;
  const auto index=static_cast<unsigned>(surface);
  if(shell.surface_registry->surfaces[index].context!=context)return false;
  auto next=std::make_shared<DesktopSurfaceRegistry>(*shell.surface_registry);
  next->surfaces[index]={};
  shell.surface_registry=std::move(next);++shell.surface_generation;return true;
}
}
