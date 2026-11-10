#include "desktop/ui/status_json.h"
namespace lunaredu::desktop {
void write_ui_status(JsonObject ui,const UiStatus &status,const char *page) {
  const auto owned=[](const char *value){return std::string(value?value:"");};
  ui["ready"] = status.ready;
  ui["page"] = owned(page?page:status.page);
  ui["menu_index"] = status.menu_index;
  ui["desktop_surface"] = owned(status.desktop_surface);
  ui["desktop_layout"] = owned(status.desktop_layout);
  ui["desktop_focus"] = owned(status.desktop_focus);
  ui["desktop_menu"] = owned(status.desktop_menu);
  ui["desktop_menu_row"] = status.desktop_menu_row;
  ui["desktop_menu_count"] = status.desktop_menu_count;
  ui["desktop_app_count"] = status.desktop_app_count;
  ui["home_indicator_visible"] = status.home_indicator_visible;
  ui["home_transition"] = status.home_transition;
  ui["home_progress"] = status.home_progress;
  ui["home_completion"] = status.home_completion;
  ui["desktop_labels"] = status.desktop_labels;
  ui["desktop_text_percent"] = status.desktop_text_percent;
  ui["desktop_rotation"] = status.desktop_rotation;
  ui["desktop_antialias"] = status.desktop_antialias;
  ui["desktop_animations"] = status.desktop_animations;
  ui["desktop_moving"] = status.desktop_moving;
  ui["desktop_panning"] = status.desktop_panning;
  ui["desktop_camera_x"] = status.desktop_camera.x;
  ui["desktop_camera_y"] = status.desktop_camera.y;
  ui["drawing_application"] = owned(status.drawing_application);
  ui["content_antialias"] = status.content_antialias;
  ui["image_sampling"] = owned(status.image_sampling);
  if(status.performance){
  const auto &performance=*status.performance;const auto &p=performance.pipeline;
  auto pipeline=ui["pipeline"].to<JsonObject>();
  pipeline["target_fps"]=p.target_fps;pipeline["coalesced_jobs"]=p.coalesced_jobs;
  pipeline["render_budget_misses"]=p.render_budget_misses;pipeline["transfer_budget_misses"]=p.transfer_budget_misses;
  pipeline["pacing_wait_us"]=p.pacing_wait_us;
  pipeline["baseline_copy_bytes"]=p.baseline_copy_bytes;
  pipeline["render_jobs"]=p.render_jobs;pipeline["rendered"]=p.rendered;pipeline["completed"]=p.completed;pipeline["errors"]=p.errors;
  pipeline["partial_frames"]=p.partial_frames;pipeline["unchanged_frames"]=p.unchanged_frames;
  pipeline["render_time_us"]=p.render_time_us;pipeline["render_max_us"]=p.render_max_us;
  pipeline["raster_time_us"]=p.raster_time_us;pipeline["raster_samples"]=p.raster_samples;pipeline["raster_max_us"]=p.raster_max_us;
  pipeline["compose_time_us"]=p.compose_time_us;pipeline["compose_samples"]=p.compose_samples;pipeline["compose_max_us"]=p.compose_max_us;
  pipeline["animation_time_us"]=p.animation_time_us;pipeline["animation_samples"]=p.animation_samples;pipeline["animation_max_us"]=p.animation_max_us;
  pipeline["wait_time_us"]=p.wait_time_us;pipeline["wait_max_us"]=p.wait_max_us;
  pipeline["compare_time_us"]=p.compare_time_us;pipeline["compare_samples"]=p.compare_samples;pipeline["compare_max_us"]=p.compare_max_us;
  pipeline["compare_settled_task_us"]=p.compare_settled_task_us;pipeline["compare_profiled_samples"]=p.compare_profiled_samples;
  pipeline["task_basis"]="monitor_only_scheduler_completed_slices";
  pipeline["transfer_time_us"]=p.transfer_time_us;pipeline["transfer_max_us"]=p.transfer_max_us;
  pipeline["frame_bytes"]=p.storage.frame_bytes;pipeline["mailbox_bytes"]=p.storage.mailbox_bytes;
  pipeline["storage_budget_bytes"]=p.storage.budget_bytes;pipeline["storage_reserved_bytes"]=p.storage.reserved_bytes;
  auto slow=pipeline["slow_stages"].to<JsonObject>();
  constexpr const char *stages[]={"render","wait","compare","raster","compose","animation","transfer"};
  for(unsigned index=0;index<7;++index){auto stage=slow[stages[index]].to<JsonObject>();
    stage["over_16_ms"]=p.over_16_ms[index];stage["over_32_ms"]=p.over_32_ms[index];}
  auto input=ui["input_queue"].to<JsonObject>();
  input["coalesced_moves"]=performance.input.coalesced_moves;input["dropped_pointers"]=performance.input.dropped_pointers;
  input["dropped_taps"]=performance.input.dropped_taps;
  input["high_water"]=performance.input.queue_high_water;input["cancelled_contacts"]=performance.input.cancelled_contacts;
  input["consumed_events"]=performance.input.consumed_events;input["queue_time_ms"]=performance.input.queue_time_ms;
  input["queue_max_ms"]=performance.input.queue_max_ms;
  input["over_16_ms"]=performance.input.over_16_ms;input["over_32_ms"]=performance.input.over_32_ms;
  auto raster=ui["raster_cache"].to<JsonObject>();
  raster["retained_bytes"]=performance.raster.retained_bytes;raster["frames"]=performance.raster.frames;
  raster["image_rebuilds"]=performance.raster.image_rebuilds;raster["image_cache_hits"]=performance.raster.image_cache_hits;
  raster["output_copy_bytes"]=performance.raster.output_copy_bytes;
  raster["output_compared_bytes"]=performance.raster.output_compared_bytes;
  raster["texture_bytes"]=performance.raster.texture_bytes;raster["texture_peak_bytes"]=performance.raster.texture_peak_bytes;
  pipeline["observed_display_storage_bytes"]=p.storage.frame_bytes+p.storage.mailbox_bytes+performance.raster.retained_bytes+performance.raster.texture_bytes;
  raster["flushes"]=performance.raster.flushes;raster["pixels"]=performance.raster.pixels;
  raster["build_time_us"]=performance.raster.build_time_us;raster["build_max_us"]=performance.raster.build_max_us;
  raster["raster_time_us"]=performance.raster.raster_time_us;raster["raster_max_us"]=performance.raster.raster_max_us;
  }
}
}
