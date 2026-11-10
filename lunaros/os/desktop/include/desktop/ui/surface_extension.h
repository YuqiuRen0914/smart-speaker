#pragma once
#include <cstdint>
#include <cstddef>
#include <memory>
#include "app/lifecycle/application_surface_state.h"
namespace lunaredu::lunaros::graphics { class Scene; struct Action; }
namespace lunaredu::desktop {
struct DesktopShell;
struct DesktopStatus;
struct DesktopMenuItem;
struct DesktopApplication;
struct DesktopSurfaceRegistry;
using DesktopSurfaceState=app::ApplicationSurfaceState;
enum class DesktopSurface : uint8_t;
enum class DesktopCommand : uint8_t;
// Optional foreground surfaces provide behavior during composition. The OS
// retains navigation and frame qualification; extensions never create tasks.
enum class DesktopRowActivation : uint8_t { DeclaredRows, Disabled };
struct DesktopSurfaceExtension {
  // App-owned input policy and optional system menu placement. Equal orders
  // retain the stable surface-slot order; negative values omit the entry.
  DesktopRowActivation row_activation = DesktopRowActivation::DeclaredRows;
  int16_t system_menu_order = -1;
  bool (*poll)(DesktopShell &, uint32_t now_ms) = nullptr;
  void (*open)(DesktopShell &) = nullptr;
  void (*close)(DesktopShell &) = nullptr;
  bool (*command)(DesktopShell &, DesktopCommand) = nullptr;
  uint64_t (*revision)(const DesktopShell &) = nullptr;
  void (*build)(lunaros::graphics::Scene &, const DesktopShell &, const DesktopStatus &) = nullptr;
  bool (*action)(DesktopShell &, lunaros::graphics::Action) = nullptr;
  void (*assets)(const DesktopShell &, uint16_t *) = nullptr;
  size_t (*rows)(const DesktopShell &) = nullptr;
  void (*choose)(DesktopShell &) = nullptr;
  const char *(*title)(const DesktopShell &) = nullptr;
  bool (*menu_action)(DesktopShell &, const DesktopMenuItem &) = nullptr;
  const char *(*menu_value)(const DesktopShell &, const char *) = nullptr;
  bool (*menu_selected)(const DesktopShell &, const DesktopMenuItem &) = nullptr;
  bool (*badge)(const DesktopShell &) = nullptr;
  const DesktopApplication *application = nullptr;
  // Foreground identity is independent of launcher metadata. Child surfaces
  // declare their owner without becoming additional launcher applications.
  const char *foreground_id = nullptr;
};
// Immutable bindings travel with frames. App bindings borrow mutable owners
// through weak references, so rendering never extends their lifetime.
bool install_desktop_surface(DesktopShell &, DesktopSurface, const DesktopSurfaceExtension &,
    std::shared_ptr<const void> context = {});
// Composition rollback removes only the exact borrowed owner being installed.
bool remove_desktop_surface(DesktopShell &, DesktopSurface, const DesktopSurfaceExtension &,
    const std::shared_ptr<const void> &context = {});
const DesktopSurfaceExtension &desktop_surface_extension(const DesktopShell &, DesktopSurface);
std::shared_ptr<const void> desktop_surface_context(const DesktopShell &, DesktopSurface,
    const DesktopSurfaceExtension &expected);
}
