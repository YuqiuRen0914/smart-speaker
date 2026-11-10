#include "desktop/shell/workspace.h"
#include "desktop/shell/compact_workspace.h"
#include "configuration/settings.h"
namespace lunaredu::desktop {
bool save_home_layout(const HomeLayout &layout){
  const auto record=encode_home_layout(layout);
  std::string error;
  return !record.empty()&&lunaros::settings_write_blob("desktop","home",record,error);
}

bool save_compact_layout(const CompactLayout &layout){
  const auto record=encode_compact_layout(layout);if(record.empty())return false;
  std::string error;return lunaros::settings_write_blob("desktop","compact",record,error);
}
bool load_compact_layout(CompactLayout &layout,bool &found){
  found=false;std::string record,error;
  const auto result=lunaros::settings_read_blob("desktop","compact",8192,record,error);
  if(result==lunaros::SettingsReadResult::Failed)return false;
  if(result==lunaros::SettingsReadResult::Missing)return true;
  found=decode_compact_layout(record,layout);return found;
}
bool load_home_layout(HomeLayout &layout,bool &found){
  std::string record,error;
  const auto result=lunaros::settings_read_blob("desktop","home",8192,record,error);
  found=result==lunaros::SettingsReadResult::Found;
  return result==lunaros::SettingsReadResult::Missing||(found&&decode_home_layout(record,layout));
}
}
