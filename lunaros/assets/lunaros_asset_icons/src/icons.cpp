#include "lunaros_asset_icons.h"
#include "link_alpha.h"
#include "mono_icons.generated.h"
#include "system_icons_lookup.generated.h"
#include "game_glyphs.generated.h"
#include <cstring>
#include <array>

namespace lunaredu::lunaros::icons {
namespace {
template<size_t N>
assets::MaskView mono(const std::array<uint8_t, N> &data, uint16_t width, uint16_t height) {
  return {data.data(), width, height, uint16_t((width + 7) / 8), assets::MaskFormat::Mono1};
}
constexpr std::array<const char *, unsigned(Icon::Count)> kNames = {
  "wifi", "link", "battery", "battery_compact", "charging",
  "microphone", "repeat", "minus", "warning", "warning_frame",
  "signal_0", "signal_1", "signal_2", "signal_3", "signal_4",
  "target", "range", "rate", "reset", "sensor", "motion", "receiver", "remote", "games", "settings", "folder", "bell",
  "status_wifi", "status_platform", "status_peer", "status_battery", "status_charging", "status_power",
  "desktop_wifi", "desktop_link", "joystick", "axes", "radio", "adjust",
  "game_snake", "game_breakout", "game_tetris", "game_tiles", "game_shooter", "game_sokoban",
  "game_race", "game_pong", "game_maze", "game_platform", "game_maze3d", "display", "store", "download", "arrow_down", "arrow_left", "arrow_up", "arrow_right", "code", "bluetooth", "pet", "music"
};
constexpr bool same_key(const char *key,const char *name) {
  while(*key && *name){
    if(*name=='_'){++name;continue;}
    const char lower=*key>='A'&&*key<='Z'?char(*key-'A'+'a'):*key;
    if(lower!=*name)return false;
    ++key;++name;
  }
  return !*key&&!*name;
}
constexpr auto index_records() {
  std::array<const system_assets::Record *,unsigned(Icon::Count)> index{};
  for(unsigned i=0;i<index.size();++i)
    for(const auto &item:system_assets::records)
      if(same_key(item.key,kNames[i])){index[i]=&item;break;}
  return index;
}
constexpr auto kGeneratedRecords=index_records();
const system_assets::Record *record(Icon id) {
  return id<Icon::Count?kGeneratedRecords[unsigned(id)]:nullptr;
}
} // namespace
assets::MaskView builtin(Icon id) {
  if(id>=Icon::GameSnake&&id<=Icon::GameMaze3d){
    constexpr const uint8_t *masks[]={game_assets::kGameGlyph0,game_assets::kGameGlyph1,game_assets::kGameGlyph2,game_assets::kGameGlyph3,game_assets::kGameGlyph4,game_assets::kGameGlyph5,game_assets::kGameGlyph6,game_assets::kGameGlyph7,game_assets::kGameGlyph8,game_assets::kGameGlyph9,game_assets::kGameGlyph10};
    return {masks[unsigned(id)-unsigned(Icon::GameSnake)],38,38,38,assets::MaskFormat::Alpha8};
  }
  if (const auto *generated=record(id)) return {generated->alpha,generated->width,generated->height,generated->stride,assets::MaskFormat::Alpha8};
  switch (id) {
    case Icon::Wifi: return mono(mono_assets::kWifi, 23, 19);
    case Icon::Link: return {builtin_assets::kLinkAlpha.data(), 24, 24, 24, assets::MaskFormat::Alpha8};
    case Icon::Battery: return mono(mono_assets::kBattery, 31, 14);
    case Icon::BatteryCompact: return mono(mono_assets::kBatteryCompact, 27, 13);
    case Icon::Charging: return mono(mono_assets::kCharging, 6, 10);
    case Icon::Microphone: return mono(mono_assets::kMicrophone, 24, 24);
    case Icon::Repeat: return mono(mono_assets::kRepeat, 24, 24);
    case Icon::Minus: return mono(mono_assets::kMinus, 24, 24);
    case Icon::Warning: return mono(mono_assets::kWarning, 7, 15);
    case Icon::WarningFrame: return mono(mono_assets::kWarningFrame, 7, 15);
    default:
      if (id >= Icon::Signal0 && id <= Icon::Signal4) {
        constexpr const uint8_t *masks[] = {mono_assets::kSignal0.data(), mono_assets::kSignal1.data(),
            mono_assets::kSignal2.data(), mono_assets::kSignal3.data(), mono_assets::kSignal4.data()};
        return {masks[unsigned(id) - unsigned(Icon::Signal0)], 22, 20, 3, assets::MaskFormat::Mono1};
      }
      return {};
  }
}
assets::MaskBounds bounds(Icon id) {
  if (const auto *generated=record(id)) return {generated->bounds[0],generated->bounds[1],generated->bounds[2],generated->bounds[3]};
  switch(id){
    case Icon::Link:return {1,1,22,22};
    case Icon::Microphone:return {4,2,16,21};
    case Icon::Repeat:return {3,2,19,21};
    case Icon::Minus:return {8,12,9,2};
    default:if(id>=Icon::Signal0&&id<=Icon::Signal4)return {0,4,22,16};break;
  }
  auto view = builtin(id);
  return {0,0,view.width,view.height};
}
const char *name(Icon id) { return id < Icon::Count ? kNames[unsigned(id)] : nullptr; }
Icon find(const char *resource,Icon fallback){if(resource)for(unsigned i=0;i<unsigned(Icon::Count);++i)if(!std::strcmp(resource,kNames[i]))return static_cast<Icon>(i);return fallback;}
size_t storage_bytes(Icon id) { const auto view = builtin(id); return view.stride * view.height; }
} // namespace lunaredu::lunaros::icons
