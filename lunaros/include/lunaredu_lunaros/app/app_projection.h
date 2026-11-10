#pragma once

#include <string>

namespace lunaredu::app {

bool setup_app_projection(std::string &error);
bool set_app_projection_available(bool available, std::string &error);

} // namespace lunaredu::app
