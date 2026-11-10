#include "ota_sdk_verification.h"
#include "recovery_internal.h"
#include "ota_install_intent.h"
#include "ota_image_policy.h"
#include "configuration/settings.h"
#include <sdkconfig.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_image_format.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <algorithm>
#include <limits>
#include <mutex>

namespace lunaredu::lunaros {
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT || CONFIG_LUNAREDU_OTA_DUAL_SLOT_LAYOUT
using namespace recovery;
extern "C" {
extern const ImageContract lunaros_recovery_image_contract
    __attribute__((section(".rodata_custom_desc"), used, aligned(4))) = {
      "LUNARV3", kProtocol,
#if CONFIG_LUNAREDU_OTA_RECOVERY_IMAGE
      Role::Recovery,
#else
      Role::Business,
#endif
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
      LUNAROS_RECOVERY_LAYOUT_ID,
#else
      "lunaros-16m-v2",
#endif
      CONFIG_LUNAREDU_OTA_RECOVERY_HARDWARE};
}
#endif
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
namespace {
std::recursive_mutex mutex;
bool initialized = false;
bool ready = false;
bool running_recovery = false;
Record record;
std::string state_error;
const esp_partition_t *factory = nullptr;
const esp_partition_t *business = nullptr;

bool fail(const char *message, std::string &error) {
  error = message;
  state_error = error;
  return false;
}
bool persist(Phase phase, uint32_t size, std::string &error,
             const uint8_t *sha256 = nullptr) {
  if (!ready) return fail("recovery_state_unavailable", error);
  if (record.generation >= std::numeric_limits<uint32_t>::max() - 1)
    return fail("recovery_generation_exhausted", error);
  Record next = record;
  ++next.generation;
  next.phase = phase;
  next.image_bytes = size;
  if (sha256) std::memcpy(next.image_sha256, sha256, 32);
  else if (phase == Phase::Writing) std::memset(next.image_sha256, 0, 32);
  seal(next);
  const std::string bytes(reinterpret_cast<const char *>(&next), sizeof(next));
  if (!settings_write_blob("recovery", next.generation % 2 ? "slot1" : "slot0",
                            bytes, error, "recovery_state")) {
    state_error = error;
    return false;
  }
  record = next;
  state_error.clear();
  error.clear();
  return true;
}
bool select_factory(std::string &error) {
  const esp_err_t result = ota_sdk_set_boot_partition(factory);
  return result == ESP_OK ? (error.clear(), true) : fail(esp_err_to_name(result), error);
}
bool check_layout(std::string &error) {
  struct Expected { const char *name; esp_partition_type_t type;
    esp_partition_subtype_t subtype; uint32_t address; uint32_t size; };
  const Expected expected[] = {
    {"nvs", ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, 0x11000, 0x5000},
    {"nvs_keys", ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, 0x16000, 0x1000},
    {"otadata", ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, 0x17000, 0x2000},
    {"phy_init", ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_PHY, 0x19000, 0x1000},
    {"recovery_state", ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, 0x1a000, 0x6000},
    {"recovery", ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, 0x20000, 0x200000},
    {"ota_0", ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, 0x220000, 0x600000},
    {"filesystem", ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, 0x820000, kFilesystemBytes},
#if CONFIG_LUNAREDU_OTA_FIELD_DIAGNOSTICS_LAYOUT
    {"coredump", ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x03), 0xf80000, 0x80000},
#endif
  };
  for (const auto &item : expected) {
    const auto *part = esp_partition_find_first(item.type, item.subtype, item.name);
    if (!part || part->address != item.address || part->size != item.size)
      return fail("recovery_layout_mismatch", error);
  }
  if (esp_ota_get_app_partition_count() != 1)
    return fail("recovery_requires_single_business_slot", error);
  factory = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "recovery");
  business = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "ota_0");
  const auto *running = esp_ota_get_running_partition();
  if (!running || (running->address != factory->address && running->address != business->address))
    return fail("recovery_running_partition_mismatch", error);
  const bool in_factory = running->address == factory->address;
  running_recovery = in_factory;
  if (in_factory != (lunaros_recovery_image_contract.role == Role::Recovery))
    return fail("recovery_image_role_mismatch", error);
  return true;
}
bool load_record(std::string &error) {
  if (!initialize_settings_partition("recovery_state", error)) {
    state_error = error;
    return false;
  }
  Record slots[2];
  const Record *loaded[2] = {};
  for (unsigned i = 0; i < 2; ++i) {
    std::string bytes;
    const auto result = settings_read_blob("recovery", i ? "slot1" : "slot0",
        sizeof(Record), bytes, error, "recovery_state");
    if (result == SettingsReadResult::Missing) continue;
    if (result != SettingsReadResult::Found || bytes.size() != sizeof(Record))
      return fail("recovery_state_unreadable", error);
    std::memcpy(&slots[i], bytes.data(), sizeof(Record));
    if (!valid(slots[i])) return fail("recovery_state_corrupt", error);
    loaded[i] = &slots[i];
  }
  const auto *selected = latest(loaded[0], loaded[1]);
  if (!selected && (loaded[0] || loaded[1])) return fail("recovery_state_conflict", error);
  record = selected ? *selected : Record{};
  return true;
}
bool factory_has_rescue_residency() {
  esp_app_desc_t app{};
  constexpr char project[] = "lunaros-bootstrap";
  if (esp_ota_get_partition_description(factory, &app) != ESP_OK ||
      std::memcmp(app.project_name, project, sizeof(project)) != 0) return false;
  unsigned version[3]{};
  size_t offset = 0;
  for (unsigned field = 0; field < 3; ++field) {
    const size_t start = offset;
    while (offset < sizeof(app.version) && app.version[offset] >= '0' && app.version[offset] <= '9') {
      if (version[field] > 100000U) return false;
      version[field] = version[field] * 10U + static_cast<unsigned>(app.version[offset++] - '0');
    }
    if (start == offset || offset == sizeof(app.version)) return false;
    if (field < 2 && app.version[offset++] != '.') return false;
  }
  if (app.version[offset] != '\0') return false;
  return version[0] > 1 || (version[0] == 1 &&
      (version[1] > 1 || (version[1] == 1 && version[2] >= 12)));
}
bool verify_boot(const esp_partition_t *part, const uint8_t *expected, std::string &error) {
  esp_image_metadata_t image{};
  const esp_partition_pos_t pos{part->address, part->size};
  esp_app_desc_t app{};
  uint8_t prefix[kContractOffset + sizeof(ImageContract)]{}, digest[32]{};
  const char *reason = nullptr;
  constexpr char project[] = "lunaros-bootstrap";
  if (ota_sdk_verify_image(ESP_IMAGE_VERIFY, &pos, &image) != ESP_OK ||
      !image.image_len || image.image_len > part->size ||
      esp_ota_check_image_validity(ESP_PARTITION_TYPE_APP, &image.image, nullptr) != ESP_OK ||
      esp_ota_get_partition_description(part, &app) != ESP_OK ||
      std::memcmp(app.project_name, project, sizeof(project)) ||
      esp_partition_read(part, 0, prefix, sizeof(prefix)) != ESP_OK ||
      !image_contract_allowed(prefix, sizeof(prefix), CONFIG_LUNAREDU_OTA_RECOVERY_HARDWARE,
                              reason, kLayout, Role::Recovery) ||
      ota_sdk_partition_sha256(part, digest) != ESP_OK ||
      (expected && std::memcmp(expected, digest, sizeof(digest))))
    return fail("ota_boot_image_verification_failed", error);
  return true;
}
}

bool ota_recovery_initialize(std::string &error) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (initialized) { error = state_error; return ready; }
  initialized = true;
  if (!check_layout(error) || !load_record(error)) return false;
  ready = true;
  error.clear();
  if (!running_recovery && record.phase == Phase::BootUpdated) {
    uint8_t digest[32]{};
    if (ota_sdk_partition_sha256(business,digest) != ESP_OK ||
        std::memcmp(digest,record.image_sha256,sizeof(digest))) {
      ready = false; return fail("ota_boot_product_identity_mismatch",error);
    }
  }
  // A reset during an unfinished handoff must not restart product outputs.
  if (!running_recovery && record.phase != Phase::BootPending && record.phase != Phase::Confirmed &&
      record.phase != Phase::BootUpdated) {
    if (!select_factory(error)) { ready = false; return false; }
    esp_restart();
    return false;
  }
  // Power may fail after SDK VALID but before our Confirmed commit. Match
  // the recorded image digest before reconciling; VALID alone is insufficient.
  if (!running_recovery && record.phase == Phase::BootPending) {
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    const esp_err_t result = esp_ota_get_state_partition(business, &state);
    if (result != ESP_OK) { ready = false; return fail(esp_err_to_name(result), error); }
    if (state == ESP_OTA_IMG_VALID) {
      uint8_t actual[32];
      if (ota_sdk_partition_sha256(business, actual) != ESP_OK ||
          std::memcmp(actual, record.image_sha256, sizeof(actual)) != 0) {
        ready = false;
        return fail("recovery_confirmation_image_mismatch", error);
      }
      if (!persist(Phase::Confirmed, record.image_bytes, error)) {
        // Keep management bootable; confirmation calls/ticks can retry commit.
        return true;
      }
    }
  }
  if (running_recovery && record.phase == Phase::BootPending)
    return persist(Phase::Rescue, record.image_bytes, error);
  if (running_recovery && record.phase == Phase::Writing)
    return persist(Phase::Failed, record.image_bytes, error);
  return true;
}
OtaRecoverySnapshot ota_recovery_snapshot() {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  std::string error;
  (void)ota_recovery_initialize(error);
  return {true, ready, running_recovery, record.generation, record.image_bytes,
          phase_name(record.phase), state_error};
}
bool ota_recovery_select_factory_for_rescue(std::string &error) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  // No ordinary configuration/service initialization is required. Damaged
  // bookkeeping must not prevent selection of independent, verified recovery.
  if (!check_layout(error)) return false;
  if (running_recovery) {
    // A replaced factory has no SDK trial state. Only the exact confirmed
    // application retained during its update can rescue a failing Boot.
    if (!load_record(error) || record.phase != Phase::BootUpdated)
      return fail("ota_boot_rescue_unavailable", error);
    uint8_t digest[32]{};
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (!record.image_bytes || record.image_bytes > business->size ||
        esp_ota_get_state_partition(business, &state) != ESP_OK || state != ESP_OTA_IMG_VALID ||
        ota_sdk_partition_sha256(business, digest) != ESP_OK ||
        std::memcmp(digest, record.image_sha256, sizeof(digest)))
      return fail("ota_boot_rescue_unavailable", error);
    esp_app_desc_t app{};
    uint8_t prefix[kContractOffset + sizeof(ImageContract)]{};
    const char *reason = nullptr;
    esp_image_metadata_t image{};
    const esp_partition_pos_t pos{business->address, business->size};
    if (esp_partition_read(business, 0, prefix, sizeof(prefix)) != ESP_OK ||
        !image_contract_allowed(prefix, sizeof(prefix), CONFIG_LUNAREDU_OTA_RECOVERY_HARDWARE,
                                reason, kLayout, Role::Business) ||
        ota_sdk_verify_image(ESP_IMAGE_VERIFY, &pos, &image) != ESP_OK ||
        esp_ota_check_image_validity(ESP_PARTITION_TYPE_APP, &image.image, nullptr) != ESP_OK ||
        esp_ota_get_partition_description(business, &app) != ESP_OK)
      return fail("ota_boot_rescue_unavailable", error);
    const esp_err_t result = ota_sdk_set_boot_partition(business);
    return result == ESP_OK ? (error.clear(), true) : fail(esp_err_to_name(result), error);
  }
  bool barrier = false;
  if (load_record(error)) {
    ready = true;
    uint8_t digest[32]{};
    if (ota_sdk_partition_sha256(business, digest) == ESP_OK)
      std::memcpy(record.image_sha256, digest, sizeof(digest));
    // Best effort: on storage failure new recovery also refuses to replay
    // intents in Confirmed/BootPending. Unknown state remains fail closed.
    barrier = record.phase == Phase::BootUpdated || persist(Phase::Rescue, record.image_bytes, error);
  }
  std::string ignored;
  const bool cleared = ota_intent_clear(ignored);
  if (!barrier && !cleared && !factory_has_rescue_residency())
    return fail("recovery_rescue_residency_unproven", error);
  return select_factory(error);
}
bool ota_recovery_stage_install_resume(std::string &error) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (!ota_recovery_initialize(error) || !running_recovery)
    return fail("recovery_install_resume_requires_factory", error);
  return persist(Phase::Requested, 0, error);
}
bool ota_recovery_repair_state(std::string &error) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  // Never infer a target from damaged records. Verify the immutable layout and
  // role from actual partitions and this image before rebuilding bookkeeping.
  if (!check_layout(error) || !running_recovery)
    return fail("recovery_repair_requires_factory", error);
  if (!initialize_settings_partition("recovery_state", error)) return false;
  if (!select_factory(error)) return false;
  record = Record{};
  initialized = ready = true;
  // An interrupted authenticated repair remains retryable from factory.
  if (!persist(Phase::Idle, 0, error) || !persist(Phase::Idle, 0, error)) {
    ready = false;
    return false;
  }
  return true;
}
bool ota_recovery_request(std::string &error) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (!ota_recovery_initialize(error)) return false;
  if (record.phase != Phase::BootUpdated && !persist(Phase::Requested, record.image_bytes, error)) return false;
  return select_factory(error);
}
bool ota_recovery_confirm_business(std::string &error) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (!ota_recovery_initialize(error) || running_recovery)
    return fail("recovery_business_not_running", error);
  esp_ota_img_states_t state;
  esp_err_t result = esp_ota_get_state_partition(business, &state);
  if (result != ESP_OK) return fail(esp_err_to_name(result), error);
  if (record.phase != Phase::BootPending && record.phase != Phase::Confirmed && record.phase != Phase::BootUpdated)
    return fail("recovery_confirmation_not_pending", error);
  if (state == ESP_OTA_IMG_PENDING_VERIFY) result = esp_ota_mark_app_valid_cancel_rollback();
  else if (state != ESP_OTA_IMG_VALID) return fail("recovery_business_state_invalid", error);
  if (result != ESP_OK) return fail(esp_err_to_name(result), error);
  if (record.phase == Phase::Confirmed || record.phase == Phase::BootUpdated) { error.clear(); return true; }
  return persist(Phase::Confirmed, record.image_bytes, error);
}
bool ota_recovery_validate_header(const uint8_t *data, size_t size, std::string &error) {
  const char *reason = nullptr;
  if (!image_contract_allowed(data, size, CONFIG_LUNAREDU_OTA_RECOVERY_HARDWARE, reason)) {
    error = reason; return false;
  }
  error.clear(); return true;
}
const esp_partition_t *ota_recovery_write_target(std::string &error) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (!ota_recovery_initialize(error)) return nullptr;
  if (!running_recovery) { fail("ota_recovery_required", error); return nullptr; }
  error.clear(); return business;
}
bool ota_recovery_begin_write(size_t size, std::string &error) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (!ota_recovery_write_target(error) || !size || size > business->size)
    return fail("ota_recovery_write_not_admitted", error);
  // Clear both OTA selectors while the old business image is still intact.
  // A power cut after this point selects the untouched factory application.
  if (!select_factory(error)) return false;
  return persist(Phase::Writing, static_cast<uint32_t>(size), error);
}
bool ota_recovery_finish_write(const esp_partition_t *target, std::string &error) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (!ready || !running_recovery || target != business || record.phase != Phase::Writing)
    return fail("recovery_finish_not_writing", error);
  uint8_t digest[32];
  const esp_err_t result = ota_sdk_partition_sha256(target, digest);
  if (result != ESP_OK) return fail(esp_err_to_name(result), error);
  // Persist before setting NEW in otadata. Failure leaves factory selected.
  return persist(Phase::BootPending, record.image_bytes, error, digest);
}
const esp_partition_t *ota_recovery_boot_write_target(size_t size, std::string &error) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (!ota_recovery_initialize(error)) return nullptr;
  if (!size || size > factory->size) { fail("ota_boot_image_exceeds_factory", error); return nullptr; }
  if (running_recovery) { fail("ota_boot_requires_application", error); return nullptr; }
  // Factory can be repaired even when it cannot boot. Keep the confirmed
  // application selected throughout the write, including all failure paths.
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  uint8_t digest[32]{};
  if (esp_ota_get_boot_partition() != business ||
      esp_ota_get_state_partition(business, &state) != ESP_OK || state != ESP_OTA_IMG_VALID ||
      (record.phase != Phase::Confirmed && record.phase != Phase::BootUpdated) ||
      ota_sdk_partition_sha256(business, digest) != ESP_OK ||
      std::memcmp(digest, record.image_sha256, sizeof(digest))) {
    fail("ota_boot_running_not_confirmed", error); return nullptr;
  }
  if (!ota_intent_clear(error) || !persist(Phase::BootUpdated, record.image_bytes, error)) return nullptr;
  return factory;
}
bool ota_recovery_finish_boot_write(const esp_partition_t *target, std::string &error) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (target != factory || !ready || running_recovery || record.phase != Phase::BootUpdated)
    return fail("ota_boot_not_writing", error);
  return verify_boot(factory, nullptr, error);
}
void ota_recovery_write_failed() {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (ready && running_recovery && record.phase == Phase::Writing) {
    std::string error;
    (void)persist(Phase::Failed, record.image_bytes, error);
  }
}
#else
bool ota_recovery_initialize(std::string &error) { error.clear(); return true; }
OtaRecoverySnapshot ota_recovery_snapshot() { return {}; }
bool ota_recovery_select_factory_for_rescue(std::string &error) { error = "recovery_layout_disabled"; return false; }
bool ota_recovery_stage_install_resume(std::string &error) { error = "recovery_layout_disabled"; return false; }
bool ota_recovery_repair_state(std::string &error) { error = "recovery_layout_disabled"; return false; }
bool ota_recovery_request(std::string &error) { error = "recovery_layout_disabled"; return false; }
bool ota_recovery_confirm_business(std::string &error) { error = "recovery_layout_disabled"; return false; }
#endif
}
