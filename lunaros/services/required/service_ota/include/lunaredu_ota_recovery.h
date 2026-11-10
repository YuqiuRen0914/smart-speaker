#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace lunaredu::lunaros {

struct OtaRecoverySnapshot {
  bool enabled = false;
  bool ready = false;
  bool recovery_running = false;
  uint32_t generation = 0;
  uint32_t image_bytes = 0;
  std::string phase;
  std::string error;
};

// Explicit boot admission. It never erases a damaged state partition.
// On a business boot, an interrupted handoff returns to recovery before
// starting product services. Disabled v2 builds return true without mutation.
bool ota_recovery_initialize(std::string &error);
OtaRecoverySnapshot ota_recovery_snapshot();

// Caller must authorize the operation and stop product outputs first.
// Persist the handoff, validate/select factory, then let the caller reboot.
bool ota_recovery_request(std::string &error);

// Explicit destructive repair of recovery bookkeeping only. The caller must
// verify administrator authority; this API refuses non-factory/active writers.
// It preserves the business image, credentials and all unrelated NVS data.
bool ota_service_repair_recovery_state(std::string &error);

// Confirm only after the product's own health gate. No network/cloud requirement
// is imposed by this low-level mechanism.
bool ota_recovery_confirm_business(std::string &error);
}
