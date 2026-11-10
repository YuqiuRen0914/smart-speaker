#include "display/adapters/co5300.h"
#include "lunaredu_device_co5300.h"
#include <limits>
namespace lunaredu::display {
namespace {
bool draw(device::co5300::Panel &panel, const uint16_t *pixels, int width,
          int height, int stride, ScreenPixelFormat format, ScreenRegion region,
          bool patch, std::string &error) {
  if (!pixels || width <= 0 || height <= 0 || stride < width ||
      region.x < 0 || region.y < 0 || region.width <= 0 || region.height <= 0 ||
      region.width > panel.width() || region.height > panel.height() ||
      region.x > int(panel.width()) - region.width ||
      region.y > int(panel.height()) - region.height ||
      (patch ? (width != region.width || height != region.height) :
               (width != panel.width() || height != panel.height())) ||
      (format != ScreenPixelFormat::Rgb565 && format != ScreenPixelFormat::Rgb565ByteSwapped) ||
      uint64_t(height - 1) * uint64_t(stride) + uint64_t(width) >
        uint64_t(std::numeric_limits<ptrdiff_t>::max()) / sizeof(uint16_t)) {
    error = "co5300_invalid_pixels_or_region"; return false;
  }
  const auto *origin = patch ? pixels : pixels + size_t(region.y) * size_t(stride) + size_t(region.x);
  return panel.write_rgb565_region(origin, region.width, region.height, stride,
      region.x, region.y, format == ScreenPixelFormat::Rgb565ByteSwapped, error);
}
bool present(void *context, ScreenFrameView frame, ScreenRegion region, std::string &error) {
  return draw(*static_cast<device::co5300::Panel *>(context), frame.pixels, frame.width,
      frame.height, frame.stride, frame.format, region, false, error);
}
bool present_patch(void *context, ScreenPatchView patch, ScreenRegion region, std::string &error) {
  return draw(*static_cast<device::co5300::Panel *>(context), patch.pixels, patch.width,
      patch.height, patch.stride, patch.format, region, true, error);
}
bool wait(void *context, std::string &error) {
  return static_cast<device::co5300::Panel *>(context)->wait_idle(error);
}
}
ScreenBackend make_co5300_backend(device::co5300::Panel &panel) {
  ScreenBackend backend{};
  backend.context = &panel; backend.width = panel.width(); backend.height = panel.height();
  backend.present = present; backend.present_patch = present_patch; backend.wait_idle = wait;
  return backend;
}
}
