#include "desktop/local/local_shell_internal.h"
#include "lunaredu_lunaros/desktop/local/local_shell.h"
namespace lunaredu::desktop {
namespace {LocalShellPorts shell; bool initialized=false;}
bool register_local_shell(const LocalShellPorts &ports) {
 if(initialized || shell.initialize || !ports.initialize || !ports.request_page) return false;
 shell=ports;
 return true;
}
bool initialize_local_shell() {
 if(initialized) return true;
 if(!shell.initialize) return false;
 initialized=true;
 shell.initialize();
 return true;
}
void request_local_shell_page(const char *route, uint32_t revision) {
 if(initialized && shell.request_page && route) shell.request_page(route,revision);
}

}
