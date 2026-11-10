#pragma once
#include "lunaros_ui_metrics.h"
#include "desktop/shell/shell.h"
#include "desktop/ui/render_policy.h"
#include "display/screen_status.h"
#include "graphics/scene_runtime.h"
#include <iterator>
#include <string>
namespace lunaredu::desktop {
struct UiPerformance {
  display::ScreenSessionSnapshot pipeline;
  lunaros::graphics::InputStats input;
  lunaros::graphics::RasterStats raster;
};
struct UiStatus {
  bool ready = false;
  const char *page = "home";
  int menu_index = 0;
  const char *desktop_surface = "launcher";
  const char *desktop_layout = "grid";
  const char *desktop_focus = "";
  bool desktop_labels = true;
  unsigned desktop_text_percent = 100;
  uint16_t desktop_rotation = 0;
  bool desktop_antialias = true, desktop_animations = true, desktop_moving = false;
  bool desktop_panning = false;
  DesktopPoint desktop_camera{};
  const char *drawing_application = "system";
  bool content_antialias = true;
  bool home_indicator_visible = false, home_transition = false;
  float home_progress = 0;
  float home_completion = 0;
  const char *image_sampling = "nearest";
  const char *desktop_menu = "";
  size_t desktop_app_count = 0;
  uint8_t desktop_menu_row = 0;
  size_t desktop_menu_count = 0;
  bool status_bar_visible = true, status_bar_auto_hide = false;
  std::string foreground_application, running_application;
  // Diagnostics travel as a bounded immutable observation, rather than large
  // value copies through every input sample and the product's task stack.
  std::shared_ptr<const UiPerformance> performance;
};
void capture_ui_status(UiStatus &status,const DesktopShell &shell,::lunaredu::lunaros::raster::RenderPolicy policy,
    bool ready,const char *page,int menu_index,const char *drawing,
    const char *foreground,const std::string &running);
UiStatus capture_ui_status(const DesktopShell &shell,::lunaredu::lunaros::raster::RenderPolicy policy,
    bool ready,const char *page,int menu_index,const char *drawing,
    const char *foreground,const std::string &running);
struct UiExecutionInfo { int render_core, display_core, touch_core, cpu_mhz; };
// A caller-supplied log sink keeps Desktop independent of platform logging.
template<class Log> void print_ui_status(Log log, const UiStatus &shell,
    const display::ScreenSessionSnapshot &pipeline, UiExecutionInfo execution) {
  log("display lifecycle=%s frames=%u frame_bytes=%llu mailbox_bytes=%llu retained_bytes=%llu",
      display::screen_lifecycle_name(pipeline.storage.lifecycle),unsigned(pipeline.storage.frame_count),
      (unsigned long long)pipeline.storage.frame_bytes,(unsigned long long)pipeline.storage.mailbox_bytes,
      (unsigned long long)pipeline.storage.retained_bytes);
  log("display control pending=%llu timeouts=%u late_replies=%u mismatched_replies=%u frame_timeouts=%u",
      (unsigned long long)pipeline.control.pending_request,unsigned(pipeline.control.control_timeouts),
      unsigned(pipeline.control.late_replies),unsigned(pipeline.control.mismatched_replies),
      unsigned(pipeline.control.frame_timeouts));
  log( "desktop page=%s surface=%s layout=%s focus=%s labels=%d rotation=%u aa=%d animations=%d moving=%d",
      shell.page, shell.desktop_surface, shell.desktop_layout, shell.desktop_focus, shell.desktop_labels,
      static_cast<unsigned>(shell.desktop_rotation),shell.desktop_antialias,shell.desktop_animations,shell.desktop_moving);
  log("Home indicator visible=%d transition=%d progress=%.3f completion=%.3f",shell.home_indicator_visible,shell.home_transition,static_cast<double>(shell.home_progress),static_cast<double>(shell.home_completion));
  log("desktop catalog apps=%u menu=%s",static_cast<unsigned>(shell.desktop_app_count),shell.desktop_menu);
  log("desktop menu row=%u count=%u",static_cast<unsigned>(shell.desktop_menu_row),static_cast<unsigned>(shell.desktop_menu_count));
  log("drawing app=%s content_aa=%d images=%s pixel_art=nearest",
      shell.drawing_application,shell.content_antialias,shell.image_sampling);
  log( "UI render=CPU%d display=CPU%d touch=CPU%d CPU=%dMHz rendered=%u completed=%u errors=%u bytes=%u transfer_us=%u partial=%u unchanged=%u",
           execution.render_core, execution.display_core, execution.touch_core, execution.cpu_mhz, (unsigned)pipeline.rendered,
           (unsigned)pipeline.completed, (unsigned)pipeline.errors,
           (unsigned)pipeline.transfer_bytes, (unsigned)pipeline.transfer_us,
           (unsigned)pipeline.partial_frames, (unsigned)pipeline.unchanged_frames);
  log( "UI job samples=%u mean_us=%llu max_us=%u transfer_mean_us=%llu transfer_max_us=%u",
           (unsigned)pipeline.render_jobs,
           (unsigned long long)(pipeline.render_jobs?pipeline.render_time_us/pipeline.render_jobs:0),
           (unsigned)pipeline.render_max_us,
           (unsigned long long)(pipeline.completed?pipeline.transfer_time_us/pipeline.completed:0),
           (unsigned)pipeline.transfer_max_us);
  log( "UI wait_total_us=%llu wait_max_us=%u compare_samples=%u compare_mean_us=%llu compare_max_us=%u",
           (unsigned long long)pipeline.wait_time_us, (unsigned)pipeline.wait_max_us,
           (unsigned)pipeline.compare_samples,
           (unsigned long long)(pipeline.compare_samples?pipeline.compare_time_us/pipeline.compare_samples:0),
           (unsigned)pipeline.compare_max_us);

}

}
