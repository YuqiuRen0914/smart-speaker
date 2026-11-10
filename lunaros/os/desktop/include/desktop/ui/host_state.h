#pragma once
#include "desktop/ui/session.h"
#include "desktop/application/catalog.h"
#include "desktop/ui/system_views.h"
namespace lunaredu::desktop {
struct UiHostState : UiSessionState {
  DesktopShell desktop;
  std::shared_ptr<const DesktopApplicationCatalog> catalog;
  app::ViewRoute page=kShellView;
  uint32_t startup_elapsed_ms=0;
  std::string unavailable_title,unavailable_reason,displayed_application;
};
}
