#include "lunaredu_device_spi_lcd_projection.h"
namespace lunaredu::device {
bool register_device_spi_lcd_surface_projection(lunaros::DeviceProjectionRegistry &, std::string &error) {
  error = "screen_driver_disabled"; return false;
}
}
