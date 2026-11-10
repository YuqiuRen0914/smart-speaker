#pragma once
#include "lunaredu_device_rgb565_transfer.h"

#include <stddef.h>
#include <stdint.h>
#include <string>

#include <esp_err.h>

#include "lunaredu_device_spi_lcd.h"

namespace lunaredu::device {

enum class DeviceSpiLcdTransferMode {
  kWaitForCompletion,
  // The source pixels may be released when the call returns, but the panel can
  // still be transmitting its internal copy. Call
  // device_spi_lcd_surface_wait_idle() at the end of the refresh batch.
  kPipeline,
};

enum class DeviceSpiLcdThreeWireDmaMode {
  InternalQueued,
  InternalPolling,
  PsramRegion,
};

// The caller must stop new draws and drain the display before changing mode.
bool device_spi_lcd_surface_set_3wire_dma_mode(
    DeviceSpiLcdThreeWireDmaMode mode, std::string *error = nullptr);

enum class DeviceSpiLcdPixelMode {
  kRgb666,
  kRgb565,
};

enum class DeviceSpiLcdSourcePixelFormat {
  kRgb565,
  // Byte-aligned RGB666; every channel is canonicalized with its low two bits
  // clear before this boundary.
  kRgb666U8x3,
};

enum class DeviceSpiLcdRgb666Order {
  kRgb,
  kBgr,
};

struct DeviceSpiLcdFullFrameBenchmark {
  DeviceSpiLcdPixelMode mode = DeviceSpiLcdPixelMode::kRgb565;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t frame_count = 0;
  uint32_t bytes_per_frame = 0;
  uint64_t total_us = 0;
  uint32_t min_frame_us = 0;
  uint32_t max_frame_us = 0;
  uint32_t average_frame_us = 0;
  uint32_t frames_per_second_x100 = 0;
};

enum class DeviceSpiLcdHardwareScrollAxis {
  kNone,
  kHorizontal,
  kVertical,
};

struct DeviceSpiLcdSurfaceStatus {
  bool configured = false;
  bool initialized = false;
  DeviceSpiLcdConfig config;
  DeviceSpiLcdStatus device_status;
  DeviceSpiLcdPixelMode pixel_mode = DeviceSpiLcdPixelMode::kRgb565;
  DeviceSpiLcdSourcePixelFormat source_pixel_format =
      DeviceSpiLcdSourcePixelFormat::kRgb565;
  std::string last_pattern;
  std::string last_error;
  uint32_t frame_count = 0;
  bool mirror_available = false;
  uint32_t mirror_generation = 0;
  uint16_t mirror_width = 0;
  uint16_t mirror_height = 0;
  bool rgb565_panel_byte_swap = true;
  bool canvas_override_active = false;
  bool hardware_scroll_supported = false;
  DeviceSpiLcdHardwareScrollAxis hardware_scroll_axis =
      DeviceSpiLcdHardwareScrollAxis::kNone;
  uint16_t hardware_scroll_native_lines = 0;
  bool hardware_scroll_active = false;
  uint16_t hardware_scroll_fixed_start = 0;
  uint16_t hardware_scroll_extent = 0;
  uint16_t hardware_scroll_fixed_end = 0;
  uint16_t hardware_scroll_offset = 0;
  bool dma_pipeline_enabled = false;
  uint8_t dma_pipeline_buffer_count = 0;
  uint16_t dma_pipeline_buffer_rows = 0;
  uint32_t dma_pipeline_buffer_pixels = 0;
  uint8_t dma_pipeline_max_in_flight = 0;
  uint32_t dma_pipeline_submitted = 0;
  uint32_t dma_pipeline_completed = 0;
  uint32_t dma_pipeline_buffer_waits = 0;
  Rgb565TransferStats rgb565_transfer;
};

struct DeviceSpiLcdSurfaceBmpWriter {
  bool (*begin_stream)(uint16_t status_code, const char *content_type,
                       void *context) = nullptr;
  bool (*send_chunk)(const uint8_t *data, size_t size, void *context) = nullptr;
  void (*end_stream)(void *context) = nullptr;
  void *context = nullptr;
};

bool device_spi_lcd_surface_setup();
// One immutable boot-lifetime observer, called on the physical setup owner.
// Binding publishes current readiness immediately; it does not own protocol state.
bool device_spi_lcd_surface_bind_availability(void *, void (*)(void *, bool));
bool device_spi_lcd_surface_ready();
// Packed 9-bit transfer chunk size configured for this firmware (zero when unsupported).
size_t device_spi_lcd_surface_3wire_packed_chunk_bytes();
bool device_spi_lcd_surface_get_panel(DeviceSpiLcdPanel *panel);
bool device_spi_lcd_surface_draw_pattern(const std::string &pattern,
                                         uint16_t color = 0x0000,
                                         std::string *error = nullptr);
bool device_spi_lcd_surface_fill(uint16_t color,
                                 std::string *error = nullptr);
bool device_spi_lcd_surface_fill_rect(uint16_t x, uint16_t y, uint16_t width,
                                      uint16_t height, uint16_t color,
                                      std::string *error = nullptr);
bool device_spi_lcd_surface_fill_rgb666(uint32_t color,
                                        std::string *error = nullptr);
bool device_spi_lcd_surface_fill_rect_rgb666(
    uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color,
    std::string *error = nullptr);
void device_spi_lcd_surface_set_canvas_override(bool active);
bool device_spi_lcd_surface_canvas_override_active();
// The mirror costs one RGB565 frame and is intended for an explicitly enabled
// local debug surface. Disabling releases its storage immediately.
void device_spi_lcd_surface_set_mirror_enabled(bool enabled);
// Bind the physical display owner's invalidation port. Captures request one
// fresh full frame, detach their temporary mirror, and release it after sending.
// The owner/port must outlive capture calls; unbind after draining callers.
void device_spi_lcd_surface_bind_redraw(void *context, void (*request)(void *));
bool device_spi_lcd_surface_draw_rgb565_cover(const uint16_t *pixels,
                                              uint16_t src_width,
                                              uint16_t src_height,
                                              bool swap_bytes,
                                              std::string *error = nullptr);
bool device_spi_lcd_surface_draw_rgb565_bitmap(uint16_t x, uint16_t y,
                                               uint16_t width,
                                               uint16_t height,
                                               const uint16_t *pixels,
                                               bool swap_bytes,
                                               std::string *error = nullptr,
                                               DeviceSpiLcdTransferMode mode =
                                                   DeviceSpiLcdTransferMode::kWaitForCompletion);
// ST7789 reads a rectangle from a wider RGB565 source. Keep the source
// immutable until this synchronous call returns. Four-wire accepts arbitrary
// widths; three-wire requires a multiple of four for byte-aligned 9-bit rows.
// swap_bytes=false denotes native RGB565 words; true denotes byte-swapped
// words. Three-wire converts native input while packing its DMA stream.
bool device_spi_lcd_surface_draw_rgb565_strided_bitmap(
    uint16_t x, uint16_t y, uint16_t width, uint16_t height,
    const uint16_t *pixels, uint16_t stride, bool swap_bytes,
    std::string *error = nullptr);
bool device_spi_lcd_surface_draw_rgb565_bitmap_background(
    uint16_t x, uint16_t y, uint16_t width, uint16_t height,
    const uint16_t *pixels, bool swap_bytes, std::string *error = nullptr);
bool device_spi_lcd_surface_draw_rgb666_bitmap(
    uint16_t x, uint16_t y, uint16_t width, uint16_t height,
    const uint8_t *pixels, DeviceSpiLcdRgb666Order order,
    std::string *error = nullptr,
    DeviceSpiLcdTransferMode mode =
        DeviceSpiLcdTransferMode::kWaitForCompletion);
bool device_spi_lcd_surface_draw_rgb666_bitmap_background(
    uint16_t x, uint16_t y, uint16_t width, uint16_t height,
    const uint8_t *pixels, DeviceSpiLcdRgb666Order order,
    std::string *error = nullptr);
bool device_spi_lcd_surface_wait_idle(std::string *error = nullptr);
bool device_spi_lcd_surface_set_pixel_mode(DeviceSpiLcdPixelMode mode,
                                            std::string *error = nullptr);
DeviceSpiLcdPixelMode device_spi_lcd_surface_pixel_mode();
DeviceSpiLcdSourcePixelFormat device_spi_lcd_surface_source_pixel_format();
bool device_spi_lcd_surface_benchmark_full_frame(
    DeviceSpiLcdPixelMode mode, uint16_t frame_count,
    DeviceSpiLcdFullFrameBenchmark *result, std::string *error = nullptr);
// Draw the diagnostic gradient once and leave the panel in the requested
// transfer mode so an operator can inspect the actual wire format.
bool device_spi_lcd_surface_show_full_frame(DeviceSpiLcdPixelMode mode,
                                             std::string *error = nullptr);
// Hardware scrolling is a controller-wide scan band, not an arbitrary
// rectangle. On an ILI9488 the band always spans the perpendicular axis and
// fixed_start + extent + fixed_end must equal 480 native lines. In landscape
// mode (swap_xy=true) that native vertical scan axis appears horizontally.
bool device_spi_lcd_surface_configure_hardware_scroll(
    DeviceSpiLcdHardwareScrollAxis axis, uint16_t fixed_start,
    uint16_t extent, uint16_t fixed_end, std::string *error = nullptr);
bool device_spi_lcd_surface_set_hardware_scroll_offset(
    uint16_t offset, std::string *error = nullptr);
bool device_spi_lcd_surface_reset_hardware_scroll(
    std::string *error = nullptr);
DeviceSpiLcdSurfaceStatus device_spi_lcd_surface_status();
// Nonblocking observation. False means busy, not a failed/unavailable panel.
bool device_spi_lcd_surface_try_status(DeviceSpiLcdSurfaceStatus &status);
esp_err_t device_spi_lcd_surface_write_bmp(
    const DeviceSpiLcdSurfaceBmpWriter &writer, std::string *error = nullptr);

} // namespace lunaredu::device
