#pragma once
#include <string>
#include "kernel/service.h"
#include "device_api/device_projection.h"
namespace lunaredu::lunaros { class ProductRuntime; }
namespace smart_speaker {
bool declare_desktop(lunaredu::lunaros::ProductRuntime &,std::string &);
lunaredu::lunaros::ServiceNode desktop_display_service(lunaredu::lunaros::ProductRuntime &);
bool start_desktop(lunaredu::lunaros::ProductRuntime &,std::string &);
bool tick_desktop(lunaredu::lunaros::ProductRuntime &);
bool python_foreground(bool,std::string &);
bool enter_music(std::string &);
void fill_desktop_status(lunaredu::lunaros::DeviceApiContext &,JsonObject);
}
