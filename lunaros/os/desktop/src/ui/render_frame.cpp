#include "diagnostics/function_probe.h"
#include "desktop/navigation/snapshot.h"
#include "desktop/ui/render_frame.h"
#include "desktop/application/downloaded_view.h"

namespace lunaredu::desktop {
void UiFrame::release_payload_leases() {
  catalog.reset();release_shell_render_leases(desktop);
  for(auto *text:{&location,&detail,&application,&title,&device_name,&network,&attention_network}){
    text->clear();if(text->capacity()>512)std::string().swap(*text);
  }
  rebind();
}
namespace {
::lunaredu::lunaros::raster::RenderPolicy application_policy(const DesktopShell &shell, const DesktopApplication *application, bool system) {
  return resolve_render_policy(shell.preferences.antialias,
      application ? application->rendering : ApplicationRenderHints{}, system || !application);
}
}
const DesktopApplication *drawing_application(const DesktopShell &shell, const char *id) {
  const auto index = shell.find_app(id);
  return index < shell.count ? &shell.apps[index] : nullptr;
}

::lunaredu::lunaros::raster::RenderPolicy drawing_policy(const DesktopShell &shell, const char *id, bool system) {
  const auto *application = drawing_application(shell, id);
  return application_policy(shell, application, system);
}

const DesktopApplication *drawing_application(const UiHostState &state) {
  return drawing_application(state.desktop,state.page==lunaros::apps::kDownloadedView?state.displayed_application.c_str():state.page.owner);
}

::lunaredu::lunaros::raster::RenderPolicy drawing_policy(const UiHostState &state,bool system) {
  const auto *application=drawing_application(state);
  return application_policy(state.desktop,application,system);
}

void capture_ui_frame(UiFrame &frame,const UiHostState &state,lunaros::graphics::Page page,
    UiFramePresentation presentation,RenderKind kind) {
  lunaredu::system::probe::Scope diagnostic_scope(lunaredu::system::probe::Point::FrameCapture);
  if(state.page==kShellView)frame.desktop=state.desktop;
  else {
    if(frame.page==kShellView)frame.release_payload_leases();
    auto &target=frame.desktop;const auto &source=state.desktop;
    // Chrome plus a faithful Launcher backdrop for Home/page motion. App
    // frames do not retain unrelated surface providers, menu identities or
    // input-only bookkeeping. Existing layout/image references stay immutable.
    target.preferences=source.preferences;target.status_navigation=source.status_navigation;
    target.home_navigation=source.home_navigation;target.edge_navigation=source.edge_navigation;
    target.apps=source.apps;target.count=source.count;target.focus=source.focus;
    target.home=source.home;target.compact=source.compact;target.wallpaper_image=source.wallpaper_image;
    target.surface=DesktopSurface::Launcher;target.camera=source.camera;
    target.grid_columns=source.grid_columns;target.grid_rows=source.grid_rows;
    std::copy_n(source.honeycomb_slots,DesktopShell::capacity,target.honeycomb_slots);
    std::copy_n(source.unread,DesktopShell::capacity,target.unread);std::copy_n(source.todo,DesktopShell::capacity,target.todo);
    target.clock_ready=source.clock_ready;target.now_ms=source.now_ms;
    target.moving=source.moving;target.frame_ms=source.frame_ms;
  }
  frame.catalog=state.catalog;frame.page=state.page;frame.ui_page=page;
  frame.content_revision=state.page==kShellView?shell_content_revision(state.desktop):page.generation;
  frame.startup_elapsed_ms=state.startup_elapsed_ms;
  frame.status=presentation.status;frame.attention=presentation.attention;
  frame.network=presentation.status.network?presentation.status.network:"";
  frame.attention_network=presentation.attention.network?presentation.attention.network:"";
  frame.location=std::move(presentation.location);frame.detail=std::move(presentation.detail);frame.device_name=std::move(presentation.device_name);
  frame.system=presentation.system;frame.unrotated=presentation.unrotated;
  frame.overlays=!frame.system&&kind!=RenderKind::Startup&&kind!=RenderKind::Diagnostic;
  frame.widgets=kind==RenderKind::Startup||state.page.widgets;
  // Every ordinary painter supplies a fully composed frame. Damage selection
  // belongs to presentation, including native Apps and Launcher, not only VM.
  frame.incremental=!frame.system&&kind!=RenderKind::Startup&&kind!=RenderKind::Diagnostic;
  const auto *application=drawing_application(state);
  frame.application=application?application->id:"";frame.title=application?application->name:"应用";
  frame.policy=application_policy(state.desktop,application,
      frame.system||frame.unrotated||kind==RenderKind::Startup||kind==RenderKind::Diagnostic);
  frame.rebind();
}

}
