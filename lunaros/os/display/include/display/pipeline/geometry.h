#pragma once
#include "display/backend.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
namespace lunaredu::display {
// Expand damage to a device's update granularity before repainting or
// staging it. Patch renderers must supply pixels for the expanded region.
bool align_screen_region(ScreenRegion region, int width, int height,
                         ScreenCapabilities capabilities,
                         ScreenRegion &aligned, std::string &error);

}
