#pragma once
#include "lunaros_asset_icons_mask.h"
#include <cstddef>

namespace lunaredu::lunaros::icons {
enum class Icon : uint8_t {
  Wifi, Link, Battery, BatteryCompact, Charging,
  Microphone, Repeat, Minus, Warning, WarningFrame,
  Signal0, Signal1, Signal2, Signal3, Signal4,
  Target, Range, Rate, Reset, Sensor, Motion, Receiver, Remote, Games,
  Settings, Folder, Bell,
  StatusWifi, StatusPlatform, StatusPeer, StatusBattery, StatusCharging, StatusPower,
  DesktopWifi, DesktopLink, Joystick, Axes, Radio, Adjust,
  GameSnake, GameBreakout, GameTetris, GameTiles, GameShooter, GameSokoban,
  GameRace, GamePong, GameMaze, GamePlatform, GameMaze3d, Display, Store, Download, ArrowDown, ArrowLeft, ArrowUp, ArrowRight, Code, Bluetooth, Pet, Music, Count
};
// Built-in immutable resource catalog. No rendering or runtime lifecycle.
assets::MaskView builtin(Icon id);
assets::MaskBounds bounds(Icon id);
const char *name(Icon id);
Icon find(const char *resource, Icon fallback = Icon::Games);
size_t storage_bytes(Icon id);
} // namespace lunaredu::lunaros::icons
