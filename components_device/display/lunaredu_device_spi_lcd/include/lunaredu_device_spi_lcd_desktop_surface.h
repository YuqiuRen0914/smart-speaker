#pragma once

#include "lunaredu_lunaros/desktop/local/lvgl_screen_runtime.h"

namespace lunaredu::device {

// Call after the SPI surface is initialized. An unavailable panel produces an
// empty port, which the Desktop runtime rejects before allocating resources.
lunaredu::desktop::LvglScreenSurface device_spi_lcd_desktop_surface();

} // namespace lunaredu::device
