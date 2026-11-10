#include "lunaredu_device_spi_lcd_desktop_surface.h"
#include "lunaredu_device_spi_lcd_surface.h"
#include <cassert>

namespace {
using namespace lunaredu::device;
bool ready = false, succeed = true;
DeviceSpiLcdPanel panel;
DeviceSpiLcdSourcePixelFormat format = DeviceSpiLcdSourcePixelFormat::kRgb565;
const uint8_t *submitted = nullptr;
unsigned calls565 = 0, calls666 = 0;
bool check(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
           const uint8_t *pixels, std::string *error, DeviceSpiLcdTransferMode mode) {
  assert(x == 3 && y == 5 && width == 2 && height == 1);
  assert(mode == DeviceSpiLcdTransferMode::kPipeline);
  submitted = pixels;
  *error = succeed ? "" : "transfer_failed";
  return succeed;
}
}
namespace lunaredu::device {
bool device_spi_lcd_surface_get_panel(DeviceSpiLcdPanel *value) { *value = panel; return ready; }
DeviceSpiLcdSourcePixelFormat device_spi_lcd_surface_source_pixel_format() { return format; }
bool device_spi_lcd_surface_draw_rgb666_bitmap(uint16_t x, uint16_t y,
    uint16_t width, uint16_t height, const uint8_t *pixels,
    DeviceSpiLcdRgb666Order order, std::string *error, DeviceSpiLcdTransferMode mode) {
  ++calls666;
  assert(order == DeviceSpiLcdRgb666Order::kBgr);
  return check(x, y, width, height, pixels, error, mode);
}
bool device_spi_lcd_surface_draw_rgb565_bitmap(uint16_t x, uint16_t y,
    uint16_t width, uint16_t height, const uint16_t *pixels, bool swap,
    std::string *error, DeviceSpiLcdTransferMode mode) {
  ++calls565;
  assert(!swap);
  return check(x, y, width, height, reinterpret_cast<const uint8_t *>(pixels), error, mode);
}
}
int main() {
  using namespace lunaredu::device;
  using Format = lunaredu::desktop::LvglScreenPixelFormat;
  assert(!device_spi_lcd_desktop_surface().draw_bitmap);
  ready = true;
  assert(!device_spi_lcd_desktop_surface().draw_bitmap);
  panel.panel = &panel; panel.width = 480; panel.height = 320;
  auto surface = device_spi_lcd_desktop_surface();
  assert(surface.width == 480 && surface.height == 320 && surface.pixel_format == Format::Rgb565);
  const uint16_t pixels[] = {0xf800, 0x07e0, 0x001f};
  const auto *bytes = reinterpret_cast<const uint8_t *>(pixels);
  std::string error;
  assert(surface.draw_bitmap(surface.context, 3, 5, 2, 1, bytes, surface.pixel_format, error));
  assert(submitted == bytes && calls565 == 1 && calls666 == 0);
  format = DeviceSpiLcdSourcePixelFormat::kRgb666U8x3;
  surface = device_spi_lcd_desktop_surface();
  assert(surface.pixel_format == Format::Bgr666U8x3);
  assert(surface.draw_bitmap(surface.context, 3, 5, 2, 1, bytes, surface.pixel_format, error));
  assert(submitted == bytes && calls565 == 1 && calls666 == 1);
  succeed = false;
  assert(!surface.draw_bitmap(surface.context, 3, 5, 2, 1, bytes, surface.pixel_format, error));
  assert(error == "transfer_failed");
  assert(!surface.draw_bitmap(surface.context, 3, 5, 2, 1, bytes, static_cast<Format>(99), error));
  assert(error == "desktop_pixel_format_unsupported" && calls666 == 2);
}
