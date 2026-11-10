#pragma once
#include <cstdint>
namespace lunaredu::desktop {
struct LocalShellPorts {
 void (*initialize)() = nullptr;
 void (*request_page)(const char *, uint32_t) = nullptr;
};
// Startup registration on the display owner, before setup_lvgl_shell_view.
bool register_local_shell(const LocalShellPorts &ports);
}
