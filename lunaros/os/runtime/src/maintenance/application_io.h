#pragma once
#include <string>
namespace lunaredu::lunaros {
class KernelRuntime;
namespace apps { class ApplicationsService; }
bool configure_application_io(KernelRuntime &, apps::ApplicationsService &, std::string &);
void poll_application_io(KernelRuntime &);
bool application_io_healthy(const KernelRuntime &);
}
