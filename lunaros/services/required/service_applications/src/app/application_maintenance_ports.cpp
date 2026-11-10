#include "lunaros_applications.h"
namespace lunaredu::lunaros::apps {
ApplicationMaintenancePorts application_maintenance_ports(ApplicationsService &service) {
  return {&service,
    [](void *p) { return static_cast<ApplicationsService *>(p)->cleanup_status(); },
    [](void *p) { return static_cast<ApplicationsService *>(p)->retry_cleanup(); }};
}
}
