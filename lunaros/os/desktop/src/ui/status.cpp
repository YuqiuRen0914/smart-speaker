#include "desktop/ui/status.h"

namespace lunaredu::desktop {
void capture_ui_status(UiStatus &status,const DesktopShell &shell,::lunaredu::lunaros::raster::RenderPolicy policy,
    bool ready,const char *page,int menu_index,const char *drawing,
    const char *foreground,const std::string &running) {
  static constexpr const char *surfaces[]={"launcher","system","settings","status","notifications","files","actions","startup","wifi","application_data"};
  static constexpr const char *layouts[]={"grid","single","honeycomb"};
  static_assert(std::size(surfaces)==static_cast<size_t>(DesktopSurface::Count));
  status.ready=ready;status.page=page;status.menu_index=menu_index;
  const auto surface=static_cast<size_t>(shell.surface),layout=static_cast<size_t>(shell.preferences.layout);
  status.desktop_surface=surface<std::size(surfaces)?surfaces[surface]:"unknown";
  status.desktop_layout=layout<std::size(layouts)?layouts[layout]:"unknown";
  status.desktop_focus=shell.apps&&shell.focus<shell.count?shell.apps[shell.focus].id:"";status.desktop_labels=shell.preferences.show_labels;
  status.desktop_rotation=static_cast<uint16_t>(shell.preferences.rotation*90);status.desktop_antialias=shell.preferences.antialias;
  status.desktop_text_percent=lunaros::graphics::metrics::text_scale_percent(shell.preferences.text_size);
  status.desktop_animations=shell.preferences.animations;status.desktop_moving=shell.moving||shell.page_drag;
  status.desktop_panning=shell.joystick_panning||(shell.contact&&shell.dragged);
  status.desktop_camera=shell.camera;
  status.drawing_application=drawing?drawing:"system";status.content_antialias=policy.antialias;
  status.home_indicator_visible=shell.home_navigation.visible;status.home_transition=shell.home_navigation.exiting();
  status.home_progress=shell.home_navigation.progress;status.home_completion=shell.home_navigation.completion;
  status.image_sampling=policy.images==::lunaredu::lunaros::raster::ImageSampling::Linear?"linear":"nearest";
  status.desktop_menu=shell.active_menu?shell.active_menu->id:"";status.desktop_app_count=shell.count;
  status.desktop_menu_row=shell.row;status.desktop_menu_count=shell.active_menu?shell.active_menu->count:0;
  status.status_bar_visible=shell.status_navigation.showing(shell.preferences.auto_hide_status);status.status_bar_auto_hide=shell.preferences.auto_hide_status;
  status.foreground_application=foreground?foreground:"";status.running_application=running;
  status.performance.reset();
}

UiStatus capture_ui_status(const DesktopShell &shell,::lunaredu::lunaros::raster::RenderPolicy policy,
    bool ready,const char *page,int menu_index,const char *drawing,
    const char *foreground,const std::string &running) {
  UiStatus status;capture_ui_status(status,shell,policy,ready,page,menu_index,drawing,foreground,running);
  return status;
}
}
