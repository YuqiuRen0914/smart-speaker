#pragma once

#include <driver/spi_master.h>
#include <esp_err.h>

#include "lunaredu_device_spi_lcd.h"

namespace lunaredu::device {

// Creates an ESP-LCD compatible, synchronous ST7789 panel facade for panels
// that encode D/C as the ninth bit on the MOSI line.
esp_err_t device_st7789_3wire_create_panel(
    spi_host_device_t spi_host, const DeviceSpiLcdConfig &config,
    esp_lcd_panel_handle_t *panel_out);

} // namespace lunaredu::device
