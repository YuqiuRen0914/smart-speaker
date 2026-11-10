#pragma once
#include "desktop/shell/shell.h"
#include "lunaros_asset_icons.h"
#include <iterator>
namespace lunaredu::desktop {
namespace icon_detail {
struct Resource { const char *name; lunaros::icons::Icon asset; };
inline constexpr Resource resources[]={
  {"target",lunaros::icons::Icon::Target},
  {"range",lunaros::icons::Icon::Range},
  {"rate",lunaros::icons::Icon::Rate},
  {"desktop_link",lunaros::icons::Icon::DesktopLink},
  {"reset",lunaros::icons::Icon::Reset},
  {"desktop_wifi",lunaros::icons::Icon::DesktopWifi},
  {"sensor",lunaros::icons::Icon::Sensor},
  {"motion",lunaros::icons::Icon::Motion},
  {"receiver",lunaros::icons::Icon::Receiver},
  {"remote",lunaros::icons::Icon::Remote},
  {"games",lunaros::icons::Icon::Games},
  {"settings",lunaros::icons::Icon::Settings},
  {"folder",lunaros::icons::Icon::Folder},
  {"bell",lunaros::icons::Icon::Bell},
  {"joystick",lunaros::icons::Icon::Joystick},
  {"axes",lunaros::icons::Icon::Axes},
  {"radio",lunaros::icons::Icon::Radio},
  {"adjust",lunaros::icons::Icon::Adjust},
  {"display",lunaros::icons::Icon::Display},
  {"store",lunaros::icons::Icon::Store},
  {"download",lunaros::icons::Icon::Download},
  {"code",lunaros::icons::Icon::Code},
  {"bluetooth",lunaros::icons::Icon::Bluetooth},
  {"pet",lunaros::icons::Icon::Pet},
  {"music",lunaros::icons::Icon::Music}
};
static_assert(std::size(resources)==static_cast<unsigned>(DesktopIcon::Music)+1);
inline constexpr Resource resource(DesktopIcon icon) {
  const auto index=static_cast<unsigned>(icon);
  return index<std::size(resources)?resources[index]:resources[static_cast<unsigned>(DesktopIcon::Settings)];
}
}
inline constexpr const char *desktop_icon_resource(DesktopIcon icon) { return icon_detail::resource(icon).name; }
inline constexpr lunaros::icons::Icon desktop_icon(DesktopIcon icon) { return icon_detail::resource(icon).asset; }
}
