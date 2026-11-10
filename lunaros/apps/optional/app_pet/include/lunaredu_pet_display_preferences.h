#pragma once
#include <string>
namespace lunaredu::pet {
enum class DisplayMode { Window, Fullscreen };
enum class DisplayModePolicy { Preference, FullscreenOnly };
void configure_display_mode_policy(DisplayModePolicy policy);
DisplayModePolicy display_mode_policy();
DisplayMode display_mode();
void default_display_mode(DisplayMode mode);
// Normalized against the shortest side, shared by both products. Window mode
// leaves room for controls/captions; fullscreen uses XiaoXing's existing scale.
int body_diameter(int width,int height);
bool set_display_mode(DisplayMode,std::string &error);
}
