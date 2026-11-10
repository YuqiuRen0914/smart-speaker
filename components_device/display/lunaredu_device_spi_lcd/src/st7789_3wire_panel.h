#pragma once

#include <cstddef>
#include <driver/spi_master.h>
#include <esp_err.h>
#include "sdkconfig.h"

#include "lunaredu_device_spi_lcd.h"
#include "lunaredu_device_spi_lcd_surface.h"

namespace lunaredu::device {

#if CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_3WIRE_DMA_CHUNK_2K
inline constexpr size_t kSt7789ThreeWireRawChunkBytes = 2048;
#elif CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_3WIRE_DMA_CHUNK_4K
inline constexpr size_t kSt7789ThreeWireRawChunkBytes = 4096;
#else
inline constexpr size_t kSt7789ThreeWireRawChunkBytes = 8192;
#endif
inline constexpr size_t kSt7789ThreeWirePackedChunkBytes =
    (kSt7789ThreeWireRawChunkBytes * 9 + 7) / 8;

// Creates an ESP-LCD compatible, synchronous ST7789 panel facade for panels
// that encode D/C as the ninth bit on the MOSI line.
esp_err_t device_st7789_3wire_create_panel(
    spi_host_device_t spi_host, const DeviceSpiLcdConfig &config,
    esp_lcd_panel_handle_t *panel_out);

// Call only after the surface has drained the previous draw. The setting is
// volatile and applies to subsequent pixel writes on this panel instance.
esp_err_t device_st7789_3wire_set_dma_mode(
    esp_lcd_panel_handle_t panel, DeviceSpiLcdThreeWireDmaMode mode);

// Preserve the source canvas; convert native RGB565 to the panel's MSB-first
// stream while packing the existing 9-bit DMA chunks. No extra pixel buffer.
esp_err_t device_st7789_3wire_draw_rgb565_strided(
    esp_lcd_panel_handle_t panel, int x, int y, int width, int height,
    const uint16_t *pixels, size_t stride, bool native_rgb565);

} // namespace lunaredu::device
