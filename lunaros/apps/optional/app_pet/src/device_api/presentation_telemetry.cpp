#include "device_api/presentation_telemetry.h"
#include "lunaredu_pet_desktop_surface.h"
#include "lunaredu_pet_render.h"
#include "lunaredu_pet_service.h"
namespace lunaredu::pet::internal {
void write_desktop_scene_performance(JsonObject p,const DesktopSceneStatus &frame){
  const auto render=render_telemetry();
  p["presentation_owner"]="os-renderer";
  p["scene_generation"]=frame.generation;p["scene_pool_busy"]=frame.pool_busy;
  p["last_raster_us"]=frame.last_raster_us;
  p["producer_fps_x100"]=render.fps_x100;p["producer_raster_us"]=render.raster_us;
  p["producer_body_us"]=render.body_us;p["producer_eyes_us"]=render.eyes_us;
  const auto *expression=current_expression();p["expression"]=expression?expression:"";
  p["producer_present_us"]=render.present_us;p["producer_max_frame_us"]=render.max_frame_us;
  p["producer_slow_frames"]=render.slow_frames;p["producer_window_frames"]=render.window_frames;
}
}
