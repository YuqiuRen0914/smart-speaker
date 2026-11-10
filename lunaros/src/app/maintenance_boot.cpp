#include "lunaredu_lunaros/app/maintenance.h"
#include "lunaredu_lunaros/delivery/boot_health.h"
namespace lunaredu::lunaros {
void tick_maintenance_boot_confirmation(bool application_ready) {
  static BootHealthGate gate;
  poll_boot_health(gate, application_ready);
}
}
