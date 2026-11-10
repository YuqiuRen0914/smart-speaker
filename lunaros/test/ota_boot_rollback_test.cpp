#include "delivery/update/ota_writer.h"
#include "lunaredu_lunaros/delivery/update.h"
#include "lunaredu_lunaros/model/device_model.h"
#include <cassert>
#include <cstdlib>
#include <iostream>

namespace {
lunaredu::lunaros::OtaWriterImageStatus image;
int inspections = 0, rollbacks = 0;
} // namespace
namespace lunaredu::lunaros {
// Unused transport/upload dependencies deliberately fail if reached.
bool activate_device_capability(const DeviceCapabilityDescriptor &,
                                std::string *) {
  return true;
}
bool ota_writer_confirm_image(std::string &) {
  image.pending_verify = false;
  return true;
}
OtaWriterMetadata ota_writer_metadata() { std::abort(); }
bool ota_writer_begin(size_t, std::string &) { std::abort(); }
bool ota_writer_write(const uint8_t *, size_t, std::string &) { std::abort(); }
bool ota_writer_complete(std::string &) { std::abort(); }
void ota_writer_abort() { std::abort(); }
void ota_writer_cooperative_yield() { std::abort(); }
bool ota_service_queue_install_from_https(const std::string &,
                                          const std::string &, size_t,
                                          uint32_t &, std::string &) {
  std::abort();
}
OtaWriterImageStatus ota_writer_image_status() {
  ++inspections;
  return image;
}
bool ota_writer_rollback_and_reboot(std::string &error) {
  ++rollbacks;
  error = "simulated_reboot_failure";
  return false;
}
namespace internal {
uint32_t system_monotonic_ms() { return 100; }
[[noreturn]] void system_restart_after_delay(uint32_t) { std::abort(); }
std::string normalize_ota_release_version(const std::string &version) {
  return version;
}
} // namespace internal
} // namespace lunaredu::lunaros

int main() {
  using namespace lunaredu::lunaros;
  // No setup_ota_service call: settings/core initialization can fail first.
  image.rollback_enabled = true;
  image.pending_verify = true;
  assert(!ota_service_rollback_running_image("core_setup_failed"));
  assert(rollbacks == 1 && inspections == 1);
  assert(ota_service_status_snapshot().message ==
         "rollback_failed:simulated_reboot_failure");
  // A confirmed image must never be invalidated using stale cached state.
  image.pending_verify = false;
  assert(!ota_service_rollback_running_image("another_failure"));
  assert(rollbacks == 1 && inspections == 2);
  assert(!ota_service_boot_pending());
  image.rollback_enabled = false;
  image.pending_verify = true;
  assert(!ota_service_rollback_running_image("rollback_disabled"));
  assert(rollbacks == 1 && inspections == 3);
  image.rollback_enabled = true;
  setup_ota_service();
  assert(ota_service_boot_pending());
  assert(ota_service_confirm_running_image());
  assert(!ota_service_boot_pending());
  assert(!ota_service_rollback_running_image("post_confirmation_failure"));
  assert(rollbacks == 1);
  std::cout << "OTA early-boot rollback and authoritative image state passed\n";
}
