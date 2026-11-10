#pragma once

#include <string>

namespace lunaredu::device {

bool install_spi_lcd_surface_projection(std::string &error);
bool set_spi_lcd_surface_projection_available(bool surface_available,
                                              std::string &error);

} // namespace lunaredu::device
