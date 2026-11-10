#pragma once
#include <ArduinoJson.h>
namespace lunaredu::pet {
struct DesktopSceneStatus;
namespace internal {void write_desktop_scene_performance(JsonObject,const DesktopSceneStatus &);}
}
