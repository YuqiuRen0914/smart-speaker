#pragma once
#include <esp_err.h>

namespace lunaredu::device::spi_lcd {
// Blocking minimum test: default parameters, about ten seconds, then cleanup.
// Call from an ESP-IDF task with exclusive access to this peripheral.
esp_err_t example(int mosi_gpio, int sclk_gpio, int cs_gpio, int dc_gpio, int reset_gpio = -1, int backlight_gpio = -1);
// ILI9488/MSP3520 panel: fixed 480x320 landscape profile; wiring only.
esp_err_t example_ili9488(int mosi_gpio, int sclk_gpio, int cs_gpio, int dc_gpio, int reset_gpio = -1, int backlight_gpio = -1);
} // namespace lunaredu::device::spi_lcd
