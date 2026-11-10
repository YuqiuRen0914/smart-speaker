#pragma once
#include "app/ui_renderer.h"
namespace lunaredu::pet {
UiDecoration ui_decoration(Page);
bool apply_local_ui_action(UiAction,std::string &);
void detach_ui_presentation();
}
