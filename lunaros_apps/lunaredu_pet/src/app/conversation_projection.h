#pragma once

#include <string>

namespace lunaredu::pet::conversation {

bool install_device_projection(std::string &error);
bool set_device_projection_available(bool service_available,
                                     std::string &error);

} // namespace lunaredu::pet::conversation
