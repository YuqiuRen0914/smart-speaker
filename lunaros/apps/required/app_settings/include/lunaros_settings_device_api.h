#pragma once
#include "desktop/shell/shell.h"
#include <string>
namespace lunaredu::lunaros { class DeviceProjectionRegistry; }
namespace lunaredu::settings {
// Product supplies its retained desktop owner and permitted startup entries.
// Storage format and web projection remain OS Settings responsibilities.
struct DesktopSettingsPort {
  desktop::DesktopPreferences (*read)() = nullptr;
  bool (*apply)(desktop::DesktopPreferences, std::string &) = nullptr;
  bool (*startup_available)(const char *) = nullptr;
};
bool register_desktop_settings_projection(lunaros::DeviceProjectionRegistry &,
    DesktopSettingsPort, std::string &);
}
