#pragma once
#include "desktop/ui/route.h"
namespace lunaredu::desktop {
inline constexpr app::ViewRoute kShellView{"system.shell",0,"menu",true};
inline constexpr app::ViewRoute kUnavailableView{"system.shell",1,"application_unavailable",true};
inline constexpr app::ViewRoute kStartupView{"system.shell",2,"startup"};
inline constexpr app::ViewRoute kDiagnosticView{"system.shell",3,"diagnostic"};
}
