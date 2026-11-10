#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include "model/rgb565_image.h"
namespace lunaredu::desktop {
enum class DesktopWallpaper : uint8_t { Solid, Ocean, Aurora, Sunset, Image };
// Shared immutable pixels: the Files owner decodes; the desktop only renders.
using DesktopImage=lunaros::Rgb565Image;
bool valid_wallpaper_path(const std::string &path);
inline const char *wallpaper_name(DesktopWallpaper value) {
  constexpr const char *names[]={"纯色","海洋","极光","暮色","自选图片"};
  const auto index=static_cast<unsigned>(value);
  return names[index<5?index:0];
}
}
