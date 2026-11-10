#include "display/pipeline/pipeline_policy.h"
#include "display/pipeline/geometry.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "screen_geometry_internal.h"

namespace lunaredu::display {

using namespace screen_detail;

bool choose_screen_pipeline(ScreenCapabilities capabilities,
                            ScreenRenderModel renderer, unsigned full_buffers,
                            ScreenPipelineMode &mode, std::string &error) {
  if (renderer == ScreenRenderModel::Patches &&
      capabilities.partial_updates) {
    mode = ScreenPipelineMode::DirectPatches;
  } else if (renderer == ScreenRenderModel::StatelessFull &&
             full_buffers >= 2 && capabilities.asynchronous_transfer) {
    mode = ScreenPipelineMode::FullFramePair;
  } else if (renderer != ScreenRenderModel::Patches && full_buffers >= 2) {
    if (!capabilities.partial_updates &&
        renderer == ScreenRenderModel::RetainedPartial) {
      error = "screen_partial_updates_unavailable";
      return false;
    }
    mode = ScreenPipelineMode::RetainedCanvasSnapshot;
  } else if (renderer != ScreenRenderModel::Patches &&
             full_buffers == 1 && !capabilities.asynchronous_transfer &&
             (renderer == ScreenRenderModel::StatelessFull ||
              capabilities.partial_updates)) {
    mode = ScreenPipelineMode::RetainedCanvasDirect;
  } else {
    error = "screen_pipeline_unavailable";
    return false;
  }
  error.clear();
  return true;
}

bool align_screen_region(ScreenRegion region, int width, int height,
                         ScreenCapabilities capabilities,
                         ScreenRegion &aligned, std::string &error) {
  aligned = {};
  if (width <= 0 || height <= 0 ||
      capabilities.x_alignment == 0 || capabilities.y_alignment == 0 ||
      !valid_region(region, width, height)) {
    error = "invalid_screen_region";
    return false;
  }
  if (region.width == 0 || region.height == 0) {
    error.clear();
    return true;
  }
  if (!capabilities.partial_updates) {
    aligned = {0, 0, width, height};
  } else {
    const auto round_up = [](int value, int step, int limit) {
      return static_cast<int>(std::min<int64_t>(
          (static_cast<int64_t>(value) + step - 1) / step * step, limit));
    };
    aligned.x = region.x / capabilities.x_alignment *
                capabilities.x_alignment;
    aligned.y = region.y / capabilities.y_alignment *
                capabilities.y_alignment;
    const int right = round_up(region.x + region.width,
                               capabilities.x_alignment, width);
    const int bottom = round_up(region.y + region.height,
                                capabilities.y_alignment, height);
    aligned.width = right - aligned.x;
    aligned.height = bottom - aligned.y;
  }
  error.clear();
  return true;
}

} // namespace lunaredu::display
