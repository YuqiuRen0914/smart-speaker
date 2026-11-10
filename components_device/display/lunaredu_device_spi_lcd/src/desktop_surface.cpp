#include "lunaredu_device_spi_lcd_desktop_surface.h"
#include "lunaredu_device_spi_lcd_surface.h"

namespace lunaredu::device {
namespace {

bool draw_bitmap(void *, uint16_t x, uint16_t y, uint16_t width,
                 uint16_t height, const uint8_t *pixels,
                 lunaredu::desktop::LvglScreenPixelFormat format,
                 std::string &error) {
  using Format = lunaredu::desktop::LvglScreenPixelFormat;
  if (format == Format::Bgr666U8x3) {
    return device_spi_lcd_surface_draw_rgb666_bitmap(
        x, y, width, height, pixels, DeviceSpiLcdRgb666Order::kBgr,
        &error, DeviceSpiLcdTransferMode::kPipeline);
  }
  if (format == Format::Rgb565) {
    return device_spi_lcd_surface_draw_rgb565_bitmap(
        x, y, width, height, reinterpret_cast<const uint16_t *>(pixels),
        false, &error, DeviceSpiLcdTransferMode::kPipeline);
  }
  error = "desktop_pixel_format_unsupported";
  return false;
}

} // namespace

lunaredu::desktop::LvglScreenSurface device_spi_lcd_desktop_surface() {
  DeviceSpiLcdPanel panel;
  if (!device_spi_lcd_surface_get_panel(&panel) || panel.panel == nullptr ||
      panel.width == 0 || panel.height == 0) return {};
  lunaredu::desktop::LvglScreenSurface surface;
  surface.width = panel.width;
  surface.height = panel.height;
  surface.pixel_format = device_spi_lcd_surface_source_pixel_format() ==
          DeviceSpiLcdSourcePixelFormat::kRgb666U8x3
      ? lunaredu::desktop::LvglScreenPixelFormat::Bgr666U8x3
      : lunaredu::desktop::LvglScreenPixelFormat::Rgb565;
  surface.draw_bitmap = draw_bitmap;
  return surface;
}

} // namespace lunaredu::device
