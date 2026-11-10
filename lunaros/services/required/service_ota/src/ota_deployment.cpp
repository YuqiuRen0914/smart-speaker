#include "ota_sdk_verification.h"
#include "lunaredu_ota.h"
#include "ota_deployment.h"
#include "recovery_internal.h"
#include <sdkconfig.h>
#include <esp_ota_ops.h>
#include <esp_app_desc.h>
#include <esp_image_format.h>
#include <cstring>
#include "ota_image_policy.h"
#include "ota_rescue.h"

namespace lunaredu::lunaros {
namespace {
const esp_partition_t *find(esp_partition_type_t type, unsigned subtype, const char *label) {
  return esp_partition_find_first(type, static_cast<esp_partition_subtype_t>(subtype), label);
}
#if CONFIG_LUNAREDU_OTA_DUAL_SLOT_LAYOUT
bool dual_layout(std::string &error) {
  // Early v2 boards differ in NVS/keys/PHY placement. Validate the app/FS
  // geometry and the unique safe otadata, never require a repartition for that.
  if (!ota_legacy_dual_layout_valid(error)) return false;
  const auto *running = esp_ota_get_running_partition();
  if (!running || (running != find(ESP_PARTITION_TYPE_APP,16,"ota_0") &&
                  running != find(ESP_PARTITION_TYPE_APP,17,"ota_1"))) {
    error = "ota_deployment_layout_mismatch"; return false;
  }
  error.clear(); return true;
}
#endif
}
bool ota_service_initialize_deployment(std::string &error) {
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  return ota_recovery_initialize(error);
#elif CONFIG_LUNAREDU_OTA_DUAL_SLOT_LAYOUT
  return dual_layout(error);
#else
  error.clear(); return true;
#endif
}
OtaDeploymentSnapshot ota_service_deployment_snapshot() {
  OtaDeploymentSnapshot result;
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  result.mode = "recovery";
  result.layout_id = recovery::kLayout;
  const auto state = ota_recovery_snapshot();
  result.ready = state.ready;
  result.firmware_role = state.recovery_running ? "recovery" : "business";
  result.recovery_required = !state.recovery_running;
  result.error = state.error;
#elif CONFIG_LUNAREDU_OTA_DUAL_SLOT_LAYOUT
  result.mode = "dual_slot";
  result.layout_id = recovery::kDualLayout;
  result.ready = dual_layout(result.error);
  const auto *description=esp_app_get_description();
  result.firmware_role = description &&
      std::memcmp(description->project_name,"lunaros-bootstrap",sizeof("lunaros-bootstrap"))==0 ? "recovery" : "business";
  // This flow preserves Bootstrap and rewrites the business slot. Layout
  // readiness alone never promises a retained, downgradeable business image.
#else
  result.mode = "legacy";
  result.ready = true;
  const auto *description=esp_app_get_description();
  result.firmware_role = description &&
      std::memcmp(description->project_name,"lunaros-bootstrap",sizeof("lunaros-bootstrap"))==0 ? "recovery" : "business";
  // Unvalidated slot count is not evidence of a verified retained business.
#endif
  const auto *business = find(ESP_PARTITION_TYPE_APP,16,"ota_0");
  const auto *filesystem = find(ESP_PARTITION_TYPE_DATA,0x83,"filesystem");
  result.business_slot_bytes = business ? business->size : 0;
  result.filesystem_bytes = filesystem ? filesystem->size : 0;
  return result;
}
bool ota_deployment_validate_header(const uint8_t *data, size_t size, std::string &error) {
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  return ota_recovery_validate_header(data,size,error);
#elif CONFIG_LUNAREDU_OTA_DUAL_SLOT_LAYOUT
  const char *reason = nullptr;
  if (!recovery::image_contract_allowed(data,size,CONFIG_LUNAREDU_OTA_RECOVERY_HARDWARE,
                                      reason,recovery::kDualLayout)) {
    error = reason; return false;
  }
  error.clear(); return true;
#else
  (void)data; (void)size; error.clear(); return true;
#endif
}
bool ota_boot_validate_header(const uint8_t *data, size_t size, std::string &error) {
  const char *hardware = nullptr;
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT || CONFIG_LUNAREDU_OTA_DUAL_SLOT_LAYOUT
  hardware = CONFIG_LUNAREDU_OTA_RECOVERY_HARDWARE;
#elif defined(CONFIG_LUNAROS_BOARD_TYPE)
  hardware = CONFIG_LUNAROS_BOARD_TYPE;
#endif
  if (!hardware) { error = "ota_boot_hardware_unavailable"; return false; }
  const char *reason = nullptr;
  if (!recovery::image_contract_allowed(data, size, hardware, reason,
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
      recovery::kLayout, recovery::Role::Recovery
#else
      recovery::kDualLayout, recovery::Role::Business
#endif
      )) { error = reason; return false; }
  esp_app_desc_t candidate{};
  std::memcpy(&candidate, data + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), sizeof(candidate));
  constexpr char project[] = "lunaros-bootstrap";
  if (candidate.magic_word != ESP_APP_DESC_MAGIC_WORD ||
      std::memcmp(candidate.project_name, project, sizeof(project)) ||
      !std::memchr(candidate.version, 0, sizeof(candidate.version))) {
    error = "ota_boot_project_required"; return false;
  }
  const OtaCandidateIdentity next{project, candidate.version, candidate.secure_version};
  if (!ota_boot_candidate_identity_allowed({}, next, error)) return false;
  // Compare against the Boot being replaced, not the product's version. A broken/unparseable old Boot remains repairable.
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  const auto *previous = find(ESP_PARTITION_TYPE_APP, 0, "recovery");
#else
  const auto *previous = esp_ota_get_next_update_partition(nullptr);
#endif
  esp_app_desc_t old{};
  esp_image_metadata_t old_image{};
  esp_ota_img_states_t old_state = ESP_OTA_IMG_UNDEFINED;
  const bool old_confirmed = previous &&
      (previous->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY ||
       (esp_ota_get_state_partition(previous,&old_state) == ESP_OK && old_state == ESP_OTA_IMG_VALID));
  const esp_partition_pos_t old_pos{previous ? previous->address : 0, previous ? previous->size : 0};
  esp_err_t previous_verification = ESP_ERR_IMAGE_INVALID;
  if (old_confirmed) {
    previous_verification = ota_sdk_verify_image(ESP_IMAGE_VERIFY,&old_pos,&old_image);
    // Failure to perform a check does not prove the old Boot is damaged and
    // must not remove its downgrade/same-version identity protection.
    if (previous_verification != ESP_OK && previous_verification != ESP_ERR_IMAGE_INVALID) {
      error = std::string("ota_boot_verification_failed:") + esp_err_to_name(previous_verification);
      return false;
    }
  }
  if (old_confirmed && previous_verification == ESP_OK &&
      esp_ota_get_partition_description(previous, &old) == ESP_OK &&
      old.magic_word == ESP_APP_DESC_MAGIC_WORD &&
      !std::memcmp(old.project_name, project, sizeof(project)) &&
      std::memchr(old.version, 0, sizeof(old.version))) {
    const OtaCandidateIdentity current{project,old.version,old.secure_version};
    const bool same = old.secure_version == candidate.secure_version &&
        !std::memcmp(old.app_elf_sha256,candidate.app_elf_sha256,sizeof(old.app_elf_sha256));
    if (!ota_boot_candidate_identity_allowed(current,next,error,same)) return false;
  }
  error.clear(); return true;
}
}
