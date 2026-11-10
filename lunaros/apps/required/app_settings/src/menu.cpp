#include "lunaros_settings_menu.h"
#include "desktop/ui/content_layout.h"
#include <cstring>
namespace lunaredu::settings {
using namespace desktop;
bool open(DesktopShell &shell) {
  if (!shell.open_menu("os.settings")) {
    shell.surface=DesktopSurface::Settings;
    shell.row=shell.list_offset=0;
    shell.cancel_contact();
  }
  return true;
}
void apply_preference(DesktopShell &s, DesktopPreferenceAction setting) {
  auto p=s.preferences;
  if(setting>=DesktopPreferenceAction::Home3x3&&setting<=DesktopPreferenceAction::Home5x5){
    const auto index=unsigned(setting)-unsigned(DesktopPreferenceAction::Home3x3);
    if(lunaros::graphics::metrics::native&&s.home.enabled&&!s.home.density(home_densities[index].columns,home_densities[index].rows))s.home.error="布局无法容纳现有组件";
    return;
  }
  if(setting==DesktopPreferenceAction::Labels)p.show_labels=!p.show_labels;
  else if(setting==DesktopPreferenceAction::Antialias)p.antialias=!p.antialias;
  else if(setting==DesktopPreferenceAction::Animations)p.animations=!p.animations;
  else if(setting==DesktopPreferenceAction::HoneycombLines)p.honeycomb_lines=!p.honeycomb_lines;
  else if(setting==DesktopPreferenceAction::TextSize)p.text_size=(p.text_size+1)%3;
  else if(setting==DesktopPreferenceAction::StatusBar)p.auto_hide_status=!p.auto_hide_status;
  else if(setting==DesktopPreferenceAction::StatusAuto)p.auto_hide_status=true;
  else if(setting==DesktopPreferenceAction::StatusAlways)p.auto_hide_status=false;
  else if(setting>=DesktopPreferenceAction::Grid&&setting<=DesktopPreferenceAction::Honeycomb)
    {if constexpr(!lunaros::graphics::metrics::native)return;
    else p.layout=static_cast<DesktopLayout>(static_cast<unsigned>(setting)-static_cast<unsigned>(DesktopPreferenceAction::Grid));}
  else if(setting>=DesktopPreferenceAction::Rotate0&&setting<=DesktopPreferenceAction::Rotate270)
    p.rotation=static_cast<uint8_t>(static_cast<unsigned>(setting)-static_cast<unsigned>(DesktopPreferenceAction::Rotate0));
  else if(setting>=DesktopPreferenceAction::WallpaperSolid&&setting<=DesktopPreferenceAction::WallpaperSunset){
    p.wallpaper=static_cast<DesktopWallpaper>(static_cast<unsigned>(setting)-static_cast<unsigned>(DesktopPreferenceAction::WallpaperSolid));
    p.wallpaper_path[0]=0;
  }
  else return;
  s.set_preferences(p);s.preferences_changed=true;
}
void choose_basic_preference(DesktopShell &s) {
    auto p=s.preferences;
    const auto row=s.row+(lunaros::graphics::metrics::native?0:2);
    if (row==0){if(s.home.enabled){const bool dense=s.home.layout->columns>=5;s.home.density(dense?3:unsigned(s.home.layout->columns)+1,dense?3:4);}else p.layout=static_cast<DesktopLayout>((static_cast<unsigned>(p.layout)+1)%3);}
    if (row==1) p.show_labels=!p.show_labels;
    if (row==2) p.rotation=(p.rotation+1)%4;
    if (row==3) p.antialias=!p.antialias;
    if (row==4) p.animations=!p.animations;
    if (row==5) {
      int current=-1;
      for(size_t i=0;i<s.count;++i)if(!std::strcmp(s.apps[i].id,p.startup_id))current=static_cast<int>(i);
      size_t next=static_cast<size_t>(current+1);
      p.startup_id[0]=0;
      if(next<s.count && std::strlen(s.apps[next].id)<sizeof(p.startup_id))std::strcpy(p.startup_id,s.apps[next].id);
    }
    if(row==6)p.auto_hide_status=!p.auto_hide_status;
    if(row==7&&lunaros::graphics::metrics::native)p.text_size=(p.text_size+1)%3;
    s.set_preferences(p); s.preferences_changed=true;
}
bool detail_for_action(const char *id, Detail &detail) {
  if (!id) return false;
  const struct {const char *id; Detail detail;} actions[]={
    {"os.network",Detail::Network},{"os.bluetooth",Detail::Bluetooth},{"os.platform",Detail::Platform},
    {"os.provision",Detail::ProvisionControl},{"os.provision.qr",Detail::Provision},
    {"os.update",Detail::Update}};
  for (const auto &action:actions) if (!std::strcmp(id,action.id)) {detail=action.detail;return true;}
  return false;
}
const char *preference_value(const DesktopShell &s, const char *id) {
  const auto &preferences=s.preferences;const auto *apps=s.apps;const auto count=s.count;
  if(id){
    if(!std::strcmp(id,"os.wallpaper"))return wallpaper_name(preferences.wallpaper);
    if(!std::strcmp(id,"os.layout")){if(s.home.enabled&&s.home.layout){const auto &l=*s.home.layout;return home_densities[(l.columns-3)*3+l.rows-3].label;}constexpr const char *names[]={"四宫格","单图标","蜂窝"};if(preferences.layout==DesktopLayout::Grid&&(s.grid_columns!=2||s.grid_rows!=2))return "网格";return names[static_cast<unsigned>(preferences.layout)];}
    if(!std::strcmp(id,"os.rotation")){constexpr const char *names[]={"底边朝下","左边朝下","顶边朝下","右边朝下"};return names[preferences.rotation];}
    if(!std::strcmp(id,"os.statusbar"))return preferences.auto_hide_status?"自动隐藏":"始终显示";
    if(!std::strcmp(id,"os.text_size")){constexpr const char *names[]={"较小","标准","较大"};return names[preferences.text_size<3?preferences.text_size:1];}
    if(!std::strcmp(id,"os.startup")){for(size_t i=0;i<count;++i)if(!std::strcmp(apps[i].id,preferences.startup_id))return apps[i].name;return "桌面";}
  }
  return nullptr;
}
bool preference_selected(const DesktopShell &s, const DesktopMenuItem &item) {
  const auto &preferences=s.preferences;
  const auto setting=item.preference;
  if(setting>=DesktopPreferenceAction::Home3x3&&setting<=DesktopPreferenceAction::Home5x5){
    const auto index=unsigned(setting)-unsigned(DesktopPreferenceAction::Home3x3);
    return s.home.enabled&&s.home.layout&&s.home.layout->columns==home_densities[index].columns&&s.home.layout->rows==home_densities[index].rows;
  }
  if(setting>=DesktopPreferenceAction::WallpaperSolid&&setting<=DesktopPreferenceAction::WallpaperSunset)
    return static_cast<unsigned>(preferences.wallpaper)==static_cast<unsigned>(setting)-static_cast<unsigned>(DesktopPreferenceAction::WallpaperSolid);
  if(!std::strcmp(item.id,"os.wallpaper.file"))return preferences.wallpaper==DesktopWallpaper::Image;
  if(setting==DesktopPreferenceAction::Labels)return preferences.show_labels;
  if(setting==DesktopPreferenceAction::Antialias)return preferences.antialias;
  if(setting==DesktopPreferenceAction::Animations)return preferences.animations;
  if(setting==DesktopPreferenceAction::HoneycombLines)return preferences.honeycomb_lines;
  if(setting==DesktopPreferenceAction::StatusBar)return !preferences.auto_hide_status;
  if(item.toggle)return !std::strcmp(s.menu_value(item.id),"开启");
  if(setting==DesktopPreferenceAction::StatusAuto)return preferences.auto_hide_status;
  if(setting==DesktopPreferenceAction::StatusAlways)return !preferences.auto_hide_status;
  if(setting>=DesktopPreferenceAction::Grid&&setting<=DesktopPreferenceAction::Honeycomb)
    return static_cast<unsigned>(preferences.layout)==static_cast<unsigned>(setting)-static_cast<unsigned>(DesktopPreferenceAction::Grid);
  if(setting>=DesktopPreferenceAction::Rotate0&&setting<=DesktopPreferenceAction::Rotate270)
    return preferences.rotation==static_cast<unsigned>(setting)-static_cast<unsigned>(DesktopPreferenceAction::Rotate0);
  return false;
}
void open_startup(DesktopShell &s) {
      s.leaf_menu=s.active_menu;s.leaf_row=s.row;s.surface=DesktopSurface::Startup;s.row=s.list_offset=0;
      for(size_t i=0;i<s.count;++i)if(!std::strcmp(s.apps[i].id,s.preferences.startup_id))s.row=static_cast<uint8_t>(i+1);
      const int visible=ContentLayout(s.preferences.auto_hide_status).rows();
      if(s.row>=visible)s.list_offset=s.row-visible+1;
      s.cancel_contact();
}
void choose_startup(DesktopShell &s) {
    if(s.row>s.count)return; // The installed app catalog may shrink while open.
    auto p=s.preferences;p.startup_id[0]=0;
    if(s.row)std::strcpy(p.startup_id,s.apps[s.row-1].id);
    s.set_preferences(p);s.preferences_changed=true;
}
}
