#pragma once
#include <ArduinoJson.h>
#include "device_api/device_commands.h"
namespace lunaredu::lunaros {
bool invoke_device_command_json(const DeviceCommandExtension &extension,
    const std::string &name, JsonVariantConst input, JsonObject output, std::string &error);
}
