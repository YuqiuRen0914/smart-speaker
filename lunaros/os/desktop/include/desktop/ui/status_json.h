#pragma once
#include "desktop/ui/status.h"
#include <ArduinoJson.h>
namespace lunaredu::desktop {
void write_ui_status(JsonObject ui,const UiStatus &status,const char *page=nullptr);
}
