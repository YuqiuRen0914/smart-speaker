#pragma once
#include "app/execution/application_maintenance.h"
#include <ArduinoJson.h>
namespace lunaredu::lunaros {
void fill_application_maintenance_payload(const apps::ApplicationMaintenancePorts &ports, JsonObject payload);
}
