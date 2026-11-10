#pragma once
#include "desktop/shell/shell.h"
#include "lunaros_ui_metrics.h"
#include <iterator>

namespace lunaredu::desktop::settings_menu {
// The Settings App owns categories and preference operations. Product adapters execute the
// network/update operations; shared detail renderers own their presentation.
// Startup choices use the registered app catalog.
inline constexpr DesktopMenuItem root[]={
  {"os.network","网络",false,DesktopIcon::Wifi,0x66d8,false,DesktopPreferenceAction::None,nullptr,false,"无线与连接"},
  {"os.desktop","显示",true,DesktopIcon::Display,0x85df,false,DesktopPreferenceAction::None,nullptr,false,"外观与桌面"},
  {"os.applications","应用",false,DesktopIcon::Games,0xb4df,false,DesktopPreferenceAction::None,nullptr,false,"安装与管理"},
  {"os.general","通用",true,DesktopIcon::Settings,0xfeb0,false,DesktopPreferenceAction::None,nullptr,false,"账号与系统"},
};
inline constexpr DesktopMenuItem display_native[]={
  {"os.layout","桌面密度",true,DesktopIcon::Games,0x245f,false,DesktopPreferenceAction::Layout},
  {"os.labels","应用名称",false,DesktopIcon::Code,0x245f,false,DesktopPreferenceAction::Labels},
  {"os.rotation","屏幕方向",true,DesktopIcon::Axes,0x245f,false,DesktopPreferenceAction::Rotation},
  {"os.antialias","抗锯齿",false,DesktopIcon::Adjust,0x245f,false,DesktopPreferenceAction::Antialias},
  {"os.animations","界面动效",false,DesktopIcon::Motion,0x245f,false,DesktopPreferenceAction::Animations},
  {"os.statusbar","系统顶栏",false,DesktopIcon::Display,0x245f,false,DesktopPreferenceAction::StatusBar},
  {"os.wallpaper","桌面壁纸",true,DesktopIcon::Display,0x245f},
  {"os.text_size","文字大小",false,DesktopIcon::Code,0x245f,false,DesktopPreferenceAction::TextSize},
};
inline constexpr DesktopMenuItem display_compact[]={
  {"os.rotation","屏幕方向",true,DesktopIcon::Axes,0x245f,false,DesktopPreferenceAction::Rotation},
  {"os.antialias","抗锯齿",false,DesktopIcon::Adjust,0x245f,false,DesktopPreferenceAction::Antialias},
  {"os.animations","界面动效",false,DesktopIcon::Motion,0x245f,false,DesktopPreferenceAction::Animations},
  {"os.statusbar","系统顶栏",false,DesktopIcon::Display,0x245f,false,DesktopPreferenceAction::StatusBar},
  {"os.wallpaper","桌面壁纸",true,DesktopIcon::Display,0x245f},
  {"os.honeycomb_lines","分区线",false,DesktopIcon::Games,0x245f,false,DesktopPreferenceAction::HoneycombLines},
};
inline constexpr DesktopMenuItem wallpaper[]={
  {"os.wallpaper.solid","纯色",false,DesktopIcon::Display,0x245f,false,DesktopPreferenceAction::WallpaperSolid},
  {"os.wallpaper.ocean","海洋",false,DesktopIcon::Display,0x245f,false,DesktopPreferenceAction::WallpaperOcean},
  {"os.wallpaper.aurora","极光",false,DesktopIcon::Display,0x245f,false,DesktopPreferenceAction::WallpaperAurora},
  {"os.wallpaper.sunset","暮色",false,DesktopIcon::Display,0x245f,false,DesktopPreferenceAction::WallpaperSunset},
  {"os.wallpaper.file","自选图片",false,DesktopIcon::Files,0x245f},
};
inline constexpr DesktopMenuItem layout[]={
  {"os.layout.3x3","3×3",false,DesktopIcon::Games,0x245f,false,DesktopPreferenceAction::Home3x3},
  {"os.layout.3x4","3×4",false,DesktopIcon::Games,0x245f,false,DesktopPreferenceAction::Home3x4},
  {"os.layout.3x5","3×5",false,DesktopIcon::Games,0x245f,false,DesktopPreferenceAction::Home3x5},
  {"os.layout.4x3","4×3",false,DesktopIcon::Games,0x245f,false,DesktopPreferenceAction::Home4x3},
  {"os.layout.4x4","4×4",false,DesktopIcon::Games,0x245f,false,DesktopPreferenceAction::Home4x4},
  {"os.layout.4x5","4×5",false,DesktopIcon::Games,0x245f,false,DesktopPreferenceAction::Home4x5},
  {"os.layout.5x3","5×3",false,DesktopIcon::Games,0x245f,false,DesktopPreferenceAction::Home5x3},
  {"os.layout.5x4","5×4",false,DesktopIcon::Games,0x245f,false,DesktopPreferenceAction::Home5x4},
  {"os.layout.5x5","5×5",false,DesktopIcon::Games,0x245f,false,DesktopPreferenceAction::Home5x5},
};
inline constexpr DesktopMenuItem rotation[]={
  {"os.rotation.0","底边朝下",false,DesktopIcon::Settings,0x245f,false,DesktopPreferenceAction::Rotate0},
  {"os.rotation.90","左边朝下",false,DesktopIcon::Settings,0x245f,false,DesktopPreferenceAction::Rotate90},
  {"os.rotation.180","顶边朝下",false,DesktopIcon::Settings,0x245f,false,DesktopPreferenceAction::Rotate180},
  {"os.rotation.270","右边朝下",false,DesktopIcon::Settings,0x245f,false,DesktopPreferenceAction::Rotate270},
};
inline constexpr DesktopMenuItem general[]={
  {"os.platform","平台配对",false,DesktopIcon::Link,0x25cc},
  {"os.update","软件更新",false,DesktopIcon::Download,0x245f},
  {"os.startup","开机应用",false,DesktopIcon::Remote,0x8c72,false,DesktopPreferenceAction::Startup},
  {"os.notice_center","通知提醒",false,DesktopIcon::Bell,0xe986},
};
inline constexpr DesktopMenu menus[]={
  {"os.settings","设置",root,std::size(root),DesktopMenuLayout::Grid2x2},
  {"os.desktop","显示",lunaros::graphics::metrics::native?display_native:display_compact,lunaros::graphics::metrics::native?std::size(display_native):std::size(display_compact)},
  {"os.wallpaper","桌面壁纸",wallpaper,std::size(wallpaper)},
#if LUNAROS_UI_SIDE != 240
  {"os.layout","桌面密度",layout,std::size(layout)},
#endif
  {"os.rotation","屏幕方向",rotation,std::size(rotation),DesktopMenuLayout::Direction},
  {"os.general","通用",general,std::size(general)},
};
} // namespace lunaredu::desktop::settings_menu

namespace lunaredu::settings {
// Register borrowed, task-free page behavior during desktop composition.
bool install_desktop_surfaces(desktop::DesktopShell &);
const desktop::DesktopSurfaceExtension &desktop_extension(desktop::DesktopSurface);
enum class Detail : uint8_t { Network, Bluetooth, ProvisionControl, Platform, Provision, Update };
bool detail_for_action(const char *, Detail &);
inline const desktop::DesktopApplication &desktop_application() {
  static const desktop::DesktopApplication application={"os.settings","设置",desktop::DesktopIcon::Settings,0x630c,desktop::DesktopAppKind::Surface,{},nullptr,desktop::DesktopAppCategory::System};
  return application;
}
bool open(desktop::DesktopShell &);
void apply_preference(desktop::DesktopShell &, desktop::DesktopPreferenceAction);
void choose_basic_preference(desktop::DesktopShell &);
void open_startup(desktop::DesktopShell &);
void choose_startup(desktop::DesktopShell &);
const char *preference_value(const desktop::DesktopShell &, const char *);
bool preference_selected(const desktop::DesktopShell &, const desktop::DesktopMenuItem &);
}
