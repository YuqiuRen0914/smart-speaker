#include "desktop/local/local_shell_internal.h"
#include "lunaredu_lunaros/desktop/local/local_shell.h"
#include <cassert>
#include <cstring>
using namespace lunaredu::desktop;
namespace {
int starts=0,pages=0;
void init(){++starts;}
void page(const char *route,uint32_t revision){assert(std::strcmp(route,"custom-app")==0 && revision==7);++pages;}
}
int main(){
 assert(!initialize_local_shell());
 request_local_shell_page("custom-app",7);
 assert(starts==0 && pages==0);
 assert(!register_local_shell({init,nullptr}));
 assert(register_local_shell({init,page}));
 assert(!register_local_shell({init,page}));
 assert(initialize_local_shell() && initialize_local_shell() && starts==1);
 request_local_shell_page(nullptr,7);
 request_local_shell_page("custom-app",7);
 assert(pages==1);
 assert(!register_local_shell({init,page}));
}
