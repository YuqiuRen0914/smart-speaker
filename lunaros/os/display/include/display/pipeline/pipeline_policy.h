#pragma once
#include "display/backend.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
namespace lunaredu::display {
enum class ScreenRenderModel : uint8_t {
  Patches,          // The renderer produces independent tiles or scanlines.
  RetainedPartial,  // The renderer changes a persistent canvas in place.
  StatelessFull,    // The renderer redraws every pixel on every frame.
};

enum class ScreenPipelineMode : uint8_t {
  DirectPatches,
  RetainedCanvasDirect,
  RetainedCanvasSnapshot,
  FullFramePair,
};

// full_buffers counts complete canvases available to the application,
// including a transfer snapshot. It does not count the driver's DMA chunks.
bool choose_screen_pipeline(ScreenCapabilities capabilities,
                            ScreenRenderModel renderer, unsigned full_buffers,
                            ScreenPipelineMode &mode, std::string &error);

}
