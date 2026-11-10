#include "desktop/shell/shell.h"
#include "configuration/settings.h"
#include "lunaros_ui_metrics.h"
#include <cstring>
namespace lunaredu::desktop {
DesktopPreferences load_desktop_preferences(const char *default_startup) {
  DesktopPreferences p;
  p.auto_hide_status=true;
  std::string record, error;
  // Display preferences may use local defaults on failure; nothing is persisted.
  const auto read=lunaros::settings_read_string("desktop","preferences",22+sizeof(p.startup_id)+sizeof(p.wallpaper_path),record,error);
  if(read==lunaros::SettingsReadResult::Missing) {
    if(default_startup && std::strlen(default_startup)<sizeof(p.startup_id))std::strcpy(p.startup_id,default_startup);
    return p;
  }
  if(read==lunaros::SettingsReadResult::Failed)return p;
  if(record.size()<22 || record.substr(0,3)!="v6|" || record[4]!='|' ||
      record[6]!='|' || record[8]!='|' || record[10]!='|' || record[12]!='|' || record[14]!='|' ||
      record[3]<'0'||record[3]>'2' || (record[5]!='0'&&record[5]!='1') ||
      record[7]<'0'||record[7]>'3' || (record[9]!='0'&&record[9]!='1') ||
      (record[11]!='0'&&record[11]!='1') || (record[13]!='0'&&record[13]!='1')) return p;
  constexpr size_t start=19;
  if(record[15]<'0'||record[15]>'2'||record[16]!='|')return p;
  if((record[17]!='0'&&record[17]!='1')||record[18]!='|')return p;
  const auto split=record.find('|',start);
  if(split==std::string::npos || split+3>record.size() || record[split+2]!='|' || record[split+1]<'0'||record[split+1]>'4')return p;
  const auto path=record.substr(split+3);
  if(path.size()>=sizeof(p.wallpaper_path)||path.find('|')!=std::string::npos)return p;
  const auto wallpaper=static_cast<DesktopWallpaper>(record[split+1]-'0');
  if(wallpaper==DesktopWallpaper::Image&&!valid_wallpaper_path(path))return p;
  if(wallpaper!=DesktopWallpaper::Image&&!path.empty())return p;
  const auto id=record.substr(start,split-start);
  if(id.size()>=sizeof(p.startup_id)) return p;
  for(char c:id) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-')) return p;
  p.layout=static_cast<DesktopLayout>(record[3]-'0');p.show_labels=record[5]=='1';
  if constexpr(!lunaros::graphics::metrics::native)p.layout=DesktopLayout::Honeycomb;
  p.rotation=record[7]-'0';p.antialias=record[9]=='1';p.animations=record[11]=='1';
  p.auto_hide_status=record[13]=='1';std::strcpy(p.startup_id,id.c_str());
  p.wallpaper=wallpaper;std::strcpy(p.wallpaper_path,path.c_str());
  p.text_size=record[15]-'0';
  p.honeycomb_lines=record[17]=='1';
  p.customized=true;
  return p;
}
bool save_desktop_preferences(DesktopPreferences p) {
  if(static_cast<unsigned>(p.layout)>2 || p.rotation>3 || p.text_size>2)return false;
  if constexpr(!lunaros::graphics::metrics::native)p.layout=DesktopLayout::Honeycomb;
  if(!std::memchr(p.startup_id,0,sizeof(p.startup_id)))return false;
  if(static_cast<unsigned>(p.wallpaper)>4||!std::memchr(p.wallpaper_path,0,sizeof(p.wallpaper_path)))return false;
  const std::string path=p.wallpaper_path;
  if((p.wallpaper==DesktopWallpaper::Image&&!valid_wallpaper_path(path)) || (p.wallpaper!=DesktopWallpaper::Image&&!path.empty()))return false;
  // One record publishes a coherent set even when power is lost during save.
  for(char c:p.startup_id){if(!c)break;if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))return false;}
  std::string record="v6|";record+=static_cast<char>('0'+static_cast<unsigned>(p.layout));
  record+=p.show_labels?"|1|":"|0|";record+=static_cast<char>('0'+p.rotation);
  record+=p.antialias?"|1|":"|0|";record+=p.animations?"1|":"0|";record+=p.auto_hide_status?"1|":"0|";
  record+=static_cast<char>('0'+p.text_size);record+='|';
  record+=p.honeycomb_lines?"1|":"0|";
  record+=p.startup_id;
  record+='|';record+=static_cast<char>('0'+static_cast<unsigned>(p.wallpaper));record+='|';record+=p.wallpaper_path;
  return lunaros::settings_set_string("desktop","preferences",record);
}
} // namespace lunaredu::desktop
