#pragma once
#include "lunaros_settings_ui_session.h"
#include "lunaros_network.h"
#include "app/execution/application_context.h"
#include "lunaros_settings_wifi_runtime.h"

namespace lunaredu::settings {
bool apply_desktop_command(lunaros::ApplicationContext &device,UiCommand command,std::string &error,
    bool (*platform_command)(lunaros::ApplicationContext &,UiCommand,std::string &)=nullptr);
}
