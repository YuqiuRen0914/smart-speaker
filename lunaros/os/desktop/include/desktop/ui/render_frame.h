#pragma once
#include "desktop/ui/host_state.h"
#include "desktop/ui/paint_port.h"
#include "desktop/application/catalog.h"
#include "desktop/ui/system_views.h"
#include "graphics/scene_runtime.h"

namespace lunaredu::desktop {
enum class RenderKind { Application, Full, Navigation, Startup, Diagnostic };
struct UiFrame {
  DesktopShell desktop;
  std::shared_ptr<const DesktopApplicationCatalog> catalog;
  app::ViewRoute page = kShellView;
  lunaros::graphics::Page ui_page{};
  uint64_t content_revision = 0;
  DesktopStatus status, attention;
  ::lunaredu::lunaros::raster::RenderPolicy policy{};
  std::string location, detail, application, title, device_name;
  std::string network, attention_network;
  bool system = false, unrotated = false, widgets = false;
  bool incremental = false, overlays = true;
  uint32_t startup_elapsed_ms=0;
  unsigned rotation() const { return unrotated || page.fixed_orientation ? 0 : desktop.preferences.rotation; }
  void rebind() {
    status.location = attention.location = location.c_str();
    status.network = network.c_str(); attention.network = attention_network.c_str();
  }
  void release_payload_leases();
};
const DesktopApplication *drawing_application(const DesktopShell &shell, const char *id);
::lunaredu::lunaros::raster::RenderPolicy drawing_policy(const DesktopShell &shell, const char *id, bool system);
const DesktopApplication *drawing_application(const UiHostState &state);
::lunaredu::lunaros::raster::RenderPolicy drawing_policy(const UiHostState &state,bool system);
struct UiFramePresentation {
  DesktopStatus status,attention;
  std::string location,detail,device_name;
  bool system=false,unrotated=false;
};
void capture_ui_frame(UiFrame &frame,const UiHostState &state,lunaros::graphics::Page page,
    UiFramePresentation presentation,RenderKind kind);
struct ShellPresentation {
  std::string unavailable_title, unavailable_reason;
  void paint(UiPaintPort &port, bool) const;
};
}
