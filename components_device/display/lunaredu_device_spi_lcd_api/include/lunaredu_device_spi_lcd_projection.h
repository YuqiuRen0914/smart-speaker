#pragma once
#include <string>
namespace lunaredu::lunaros { class DeviceProjectionRegistry; }
namespace lunaredu::device {
// Boot composition declares the protocol independently from the physical driver.
bool register_device_spi_lcd_surface_projection(
    lunaredu::lunaros::DeviceProjectionRegistry &, std::string &error);
}
