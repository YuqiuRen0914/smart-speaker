#pragma once
#include "device_api/device_api_context.h"
#include <ArduinoJson.h>
namespace lunaredu::lunaros {
void fill_bootstrap_metrics_payload(const DeviceApiContext &,JsonObject);
}
