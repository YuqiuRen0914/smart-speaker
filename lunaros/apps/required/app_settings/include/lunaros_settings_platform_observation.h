#pragma once
#include "lunaros_settings_detail.h"
#include <cstdint>
namespace lunaredu::settings {
enum class UiCommand;
// Main/UI-owned optional platform presentation. Reads service facts without
// starting cloud work. Products opt into avatars and choose command exposure.
void observe_platform_settings(SystemPresentation &,int64_t now_us,bool avatar=true);
bool platform_settings_command(UiCommand,std::string &error);
}
