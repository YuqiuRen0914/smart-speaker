#pragma once
#include <cstdint>
#include <string>
namespace lunaredu::device {
enum class DeviceSpiLcdSourcePixelFormat { kRgb565, kRgb666U8x3 };
enum class DeviceSpiLcdRgb666Order { kRgb, kBgr };
enum class DeviceSpiLcdTransferMode { kWaitForCompletion, kPipeline };
struct DeviceSpiLcdPanel { void *panel = nullptr; uint16_t width = 0, height = 0; };
bool device_spi_lcd_surface_get_panel(DeviceSpiLcdPanel *);
DeviceSpiLcdSourcePixelFormat device_spi_lcd_surface_source_pixel_format();
bool device_spi_lcd_surface_draw_rgb666_bitmap(uint16_t, uint16_t, uint16_t,
    uint16_t, const uint8_t *, DeviceSpiLcdRgb666Order, std::string *, DeviceSpiLcdTransferMode);
bool device_spi_lcd_surface_draw_rgb565_bitmap(uint16_t, uint16_t, uint16_t,
    uint16_t, const uint16_t *, bool, std::string *, DeviceSpiLcdTransferMode);
}
