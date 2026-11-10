#include "app/codec/application_maintenance_json.h"

namespace lunaredu::lunaros {
void fill_application_maintenance_payload(const apps::ApplicationMaintenancePorts &ports, JsonObject payload) {
  const auto status = ports.status ? ports.status(ports.context) : apps::ApplicationCleanupStatus{false, false, 0, 0, "applications_service_unavailable"};
  payload["pending"] = status.pending;
  payload["running"] = status.running;
  payload["attempts"] = status.attempts;
  payload["retiring_owners"] = status.retiring_owners;
  payload["detail"] = status.detail;
}
}
