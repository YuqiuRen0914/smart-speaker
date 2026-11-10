#pragma once

#include <esp_err.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>

struct LunareduIli9488DmaPipelineStatus {
  uint32_t submitted_transfers = 0;
  uint32_t completed_transfers = 0;
  uint32_t buffer_wait_count = 0;
  uint8_t max_in_flight = 0;
  uint8_t buffer_count = 0;
  uint16_t buffer_rows = 0;
  uint32_t buffer_pixels = 0;
};

enum class LunareduIli9488PixelMode : uint8_t {
  kRgb666,
  kRgb565,
};

enum class LunareduIli9488Rgb666Order : uint8_t {
  kRgb,
  kBgr,
};

esp_err_t lunaredu_esp_lcd_new_panel_ili9488(
    esp_lcd_panel_io_handle_t io,
    const esp_lcd_panel_dev_config_t *panel_dev_config,
    esp_lcd_panel_handle_t *ret_panel);

// ILI9488 hardware scrolling always operates on the controller's native
// 480-line scan axis. The three areas must therefore add up to 480 even when
// MADCTL presents the frame buffer as a 480x320 landscape surface.
esp_err_t lunaredu_esp_lcd_ili9488_configure_vertical_scroll(
    esp_lcd_panel_handle_t panel, uint16_t top_fixed_lines,
    uint16_t scroll_lines, uint16_t bottom_fixed_lines);
esp_err_t lunaredu_esp_lcd_ili9488_set_vertical_scroll_start(
    esp_lcd_panel_handle_t panel, uint16_t start_line);
esp_err_t lunaredu_esp_lcd_ili9488_get_dma_pipeline_status(
    esp_lcd_panel_handle_t panel,
    LunareduIli9488DmaPipelineStatus *status);
// Wait until every panel-owned DMA buffer has completed. Unlike the generic
// esp_lcd parameter barrier, this wait is bounded so a lost SPI completion
// cannot permanently block the caller.
esp_err_t lunaredu_esp_lcd_ili9488_wait_idle(
    esp_lcd_panel_handle_t panel);
esp_err_t lunaredu_esp_lcd_ili9488_set_pixel_mode(
    esp_lcd_panel_handle_t panel, LunareduIli9488PixelMode mode);
esp_err_t lunaredu_esp_lcd_ili9488_get_pixel_mode(
    esp_lcd_panel_handle_t panel, LunareduIli9488PixelMode *mode);
// Draw byte-aligned RGB666 source pixels through the ILI9488 18-bit SPI path.
// The driver defensively clears each channel's two unused low bits.
esp_err_t lunaredu_esp_lcd_ili9488_draw_bitmap_rgb666(
    esp_lcd_panel_handle_t panel, int x_start, int y_start, int x_end,
    int y_end, const uint8_t *pixels, LunareduIli9488Rgb666Order order);
