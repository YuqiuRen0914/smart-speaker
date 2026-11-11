#pragma once
#include <ArduinoJson.h>
#include <mutex>
namespace lunaredu::pet::conversation::internal {
void handle_lunarlink_capability_locked(JsonDocument &request, std::unique_lock<std::mutex> &lock);
}
