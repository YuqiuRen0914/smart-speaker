#include "app/execution/application_context.h"
#include "app/execution/application_maintenance.h"
namespace lunaredu::lunaros::apps {
const ApplicationMaintenancePorts &application_maintenance(const ApplicationContext &device) {
  static const ApplicationMaintenancePorts empty;
  const auto *ports = device.application_maintenance_ports();
  return ports ? *ports : empty;
}
}
