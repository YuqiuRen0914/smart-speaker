#pragma once
#include <ArduinoJson.h>
namespace lunaredu::lunaros {
// On-demand compatibility projections of Core-owned observations. No worker,
// sampling session, raw SDK task pointer or product-owned counter cache.
void write_task_runtime_counters(JsonObject);
void write_system_runtime_diagnostics(JsonObject);
}
