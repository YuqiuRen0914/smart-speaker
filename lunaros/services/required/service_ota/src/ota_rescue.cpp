#include "ota_sdk_verification.h"
#include "ota_verification_retry.h"
#include "ota_rescue.h"
#include "recovery_contract.h"
#include "lunaredu_ota.h"
#include <sdkconfig.h>
#include <esp_ota_ops.h>
#include <esp_image_format.h>
#include <cstring>
#include <mutex>

namespace lunaredu::lunaros {
namespace {
bool refuse(std::string &error, const char *reason) {
  error = reason;
  return false;
}
bool bootstrap(const esp_app_desc_t &app) {
  constexpr char project[] = "lunaros-bootstrap";
  return std::memcmp(app.project_name, project, sizeof(project)) == 0;
}
bool dual_layout(std::string &error) {
  struct Expected { const char *label; esp_partition_type_t type;
    unsigned subtype, address, size; };
  constexpr Expected expected[] = {
    {"ota_0",ESP_PARTITION_TYPE_APP,16,0x20000,0x600000},
    {"ota_1",ESP_PARTITION_TYPE_APP,17,0x620000,0x600000},
    {"filesystem",ESP_PARTITION_TYPE_DATA,0x83,0xC20000,0x3E0000}};
  for (const auto &entry : expected) {
    const auto *part = esp_partition_find_first(entry.type,
        static_cast<esp_partition_subtype_t>(entry.subtype), entry.label);
    if (!part || part->address != entry.address || part->size != entry.size)
      return refuse(error, "ota_rescue_layout_mismatch");
  }
  if (esp_ota_get_app_partition_count() != 2 ||
      esp_partition_find_first(ESP_PARTITION_TYPE_APP,
          ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr))
    return refuse(error, "ota_rescue_layout_mismatch");
  // Historical v2 NVS/key/PHY placement is unrelated to rescue. SDK has
  // parsed the table; independently constrain the only metadata we mutate.
  const auto iterator = esp_partition_find(ESP_PARTITION_TYPE_DATA,
      ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if (!iterator) return refuse(error, "ota_rescue_otadata_invalid");
  const esp_partition_t *metadata = esp_partition_get(iterator);
  const auto next = esp_partition_next(iterator); // consumes iterator
  const bool duplicate = next != nullptr;
  if (next) esp_partition_iterator_release(next);
  if (duplicate || !metadata || metadata->address < 0x9000 ||
      metadata->address % 0x1000 || metadata->size < 0x2000 || metadata->size % 0x1000 ||
      metadata->address >= 0x1000000 || metadata->size > 0x1000000 - metadata->address)
    return refuse(error, "ota_rescue_otadata_invalid");
  const uint32_t end = metadata->address + metadata->size;
  for (const auto &entry : expected) {
    if (metadata->address < entry.address + entry.size && end > entry.address)
      return refuse(error, "ota_rescue_otadata_invalid");
  }
  return true;
}
bool contract_matches(const esp_partition_t *running,
                      const esp_partition_t *target, std::string &error) {
  constexpr size_t size = recovery::kContractOffset + sizeof(recovery::ImageContract);
  uint8_t current[size]{}, candidate[size]{};
  if (esp_partition_read(running, 0, current, size) != ESP_OK ||
      esp_partition_read(target, 0, candidate, size) != ESP_OK)
    return refuse(error, "ota_rescue_header_unreadable");
  // Legacy generic Bootstrap predates this declaration. A declared deployment
  // must never silently fall back to that compatibility exception.
  const auto marked = [](const uint8_t *data) {
    return std::memcmp(data + recovery::kContractOffset, "LUNAR", 5) == 0;
  };
  const char *hardware = nullptr;
#if CONFIG_LUNAREDU_OTA_DUAL_SLOT_LAYOUT
  hardware = CONFIG_LUNAREDU_OTA_RECOVERY_HARDWARE;
#else
  recovery::ImageContract contract{};
  if (marked(current)) {
    std::memcpy(&contract, current + recovery::kContractOffset, sizeof(contract));
    if (!std::memchr(contract.hardware, '\0', sizeof(contract.hardware)) ||
        contract.hardware[0] == '\0')
      return refuse(error, "ota_rescue_hardware_unverifiable");
    hardware = contract.hardware;
  }
#endif
  if (!hardware && !marked(candidate)) return true;
  if (!hardware) return refuse(error, "ota_rescue_hardware_unverifiable");
  const char *reason = nullptr;
  if (!recovery::image_contract_allowed(current, size, hardware, reason, recovery::kDualLayout) ||
      !recovery::image_contract_allowed(candidate, size, hardware, reason, recovery::kDualLayout))
    return refuse(error, reason);
  return true;
}
}

bool ota_legacy_dual_layout_valid(std::string &error) { return dual_layout(error); }
bool ota_bootstrap_update_admitted(std::string &error) {
  if (!dual_layout(error)) return false;
  const auto *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  if (!running || (running->address != 0x20000 && running->address != 0x620000) ||
      esp_ota_get_boot_partition() != running ||
      esp_ota_get_state_partition(running, &state) != ESP_OK || state != ESP_OTA_IMG_VALID)
    return refuse(error, "ota_boot_running_not_confirmed");
  error.clear(); return true;
}

static bool find_bootstrap_rescue_target(const esp_partition_t *&target, std::string &error, bool allow_application = false, bool *retryable = nullptr) {
  if (retryable) *retryable = false;
  if (!dual_layout(error)) return false;
  const auto *running = esp_ota_get_running_partition();
  const auto *slot0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
      ESP_PARTITION_SUBTYPE_APP_OTA_0, "ota_0");
  const auto *slot1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
      static_cast<esp_partition_subtype_t>(17), "ota_1");
  if (!running || (running != slot0 && running != slot1) ||
      esp_ota_get_boot_partition() != running)
    return refuse(error, "ota_rescue_selection_not_stable");
  target = running == slot0 ? slot1 : slot0;
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK || state != ESP_OTA_IMG_VALID)
    return refuse(error, "ota_rescue_running_not_confirmed");
  if (esp_ota_get_state_partition(target, &state) != ESP_OK || state != ESP_OTA_IMG_VALID)
    return refuse(error, "ota_rescue_target_not_confirmed");
  esp_app_desc_t current{}, candidate{};
  if (esp_ota_get_partition_description(running, &current) != ESP_OK ||
      esp_ota_get_partition_description(target, &candidate) != ESP_OK)
    return refuse(error, "ota_rescue_identity_unavailable");
  if (bootstrap(current)) {
    if (!allow_application || bootstrap(candidate)) return refuse(error, "ota_rescue_application_required");
  } else if (!bootstrap(candidate)) return refuse(error, "ota_rescue_bootstrap_required");
  if (!contract_matches(running, target, error)) {
    if (retryable) *retryable = error == "ota_rescue_header_unreadable";
    return false;
  }
  esp_image_metadata_t image{};
  const esp_partition_pos_t position{target->address, target->size};
  const esp_err_t verification = ota_sdk_verify_image(ESP_IMAGE_VERIFY, &position, &image);
  if (verification != ESP_OK) {
    if (verification == ESP_ERR_IMAGE_INVALID) return refuse(error, "ota_rescue_image_invalid");
    if (retryable) *retryable = true;
    error = std::string("ota_rescue_verification_failed:") + esp_err_to_name(verification);
    return false;
  }
  if (image.image_len == 0 || image.image_len > target->size)
    return refuse(error, "ota_rescue_image_invalid");
  // Rescue selects an already confirmed image, not a new image inheriting the
  // running product's SPI configuration. Bootstrap may intentionally use DIO
  // while the product uses QIO. Retain SDK chip/revision checks, but do not
  // require these two application descriptors to advertise identical modes.
  if (esp_ota_check_image_validity(ESP_PARTITION_TYPE_APP, &image.image, nullptr) != ESP_OK)
    return refuse(error, "ota_rescue_chip_incompatible");
  // set_boot may erase an anti-rollback-ineligible partition. Rescue must not
  // have this destructive failure mode; leave such deployments to management.
#if CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
  return refuse(error, "ota_rescue_anti_rollback_requires_management");
#else
  error.clear();
  return true;
#endif
}
const esp_partition_t *ota_bootstrap_rescue_target(std::string &error) {
  const esp_partition_t *target = nullptr;
  return find_bootstrap_rescue_target(target, error) ? target : nullptr;
}
bool ota_select_bootstrap_rescue(std::string &error) {
  const esp_partition_t *target = nullptr;
  if (!find_bootstrap_rescue_target(target, error, true)) return false;
  const auto *app = esp_app_get_description();
  if (app && bootstrap(*app)) {
    // A failed Boot must not remain a VALID automatic rescue target for the
    // application. SDK invalidates only this image and retains the valid app.
    const auto result = ota_sdk_rollback_and_reboot();
    return refuse(error, esp_err_to_name(result));
  }
  const esp_err_t result = ota_sdk_set_boot_partition(target);
  if (result != ESP_OK) return refuse(error, esp_err_to_name(result));
  error.clear();
  return true;
}
OtaRecoveryTargetSnapshot ota_service_recovery_target_snapshot() {
  // Successful observations follow selector/target identity/write generations.
  // Transient SDK failures retry at a bounded rate rather than poisoning the
  // running session. Status polling must not repeatedly scan a whole image.
  // Neither rescue selection nor writer admission trusts this cache.
  static std::mutex mutex;
  static OtaRecoveryTargetSnapshot cached;
#if !CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  static const esp_partition_t *cached_running = nullptr, *cached_boot = nullptr;
  static esp_ota_img_states_t cached_state = ESP_OTA_IMG_UNDEFINED;
  static esp_err_t cached_state_result = ESP_FAIL;
  static bool attempted = false;
  static uint32_t cached_generation = 0;
  static esp_ota_img_states_t cached_target_state = ESP_OTA_IMG_UNDEFINED;
  static esp_err_t cached_target_state_result = ESP_FAIL, cached_descriptor_result = ESP_FAIL;
  static esp_app_desc_t cached_descriptor{};
  static OtaVerificationRetry retry;
#endif
  std::lock_guard<std::mutex> lock(mutex);
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  cached.error = "recovery_target_factory_layout";
#else
  if (ota_sdk_image_write_active().load()) {
    OtaRecoveryTargetSnapshot busy;
    busy.error = "ota_rescue_install_active";
    return busy;
  }
  const auto *running = esp_ota_get_running_partition();
  const auto *boot = esp_ota_get_boot_partition();
  const auto *alternate = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
      running && running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0
          ? static_cast<esp_partition_subtype_t>(17) : ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
  esp_ota_img_states_t target_state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t target_state_result = alternate ? esp_ota_get_state_partition(alternate, &target_state) : ESP_FAIL;
  esp_app_desc_t descriptor{};
  const esp_err_t descriptor_result = alternate ? esp_ota_get_partition_description(alternate, &descriptor) : ESP_FAIL;
  const auto generation = ota_sdk_image_generation().load();
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t state_result = running ? esp_ota_get_state_partition(running, &state) : ESP_FAIL;
  if (attempted && cached_running == running && cached_boot == boot &&
      cached_state == state && cached_state_result == state_result &&
      cached_target_state == target_state && cached_target_state_result == target_state_result &&
      cached_descriptor_result == descriptor_result && cached_generation == generation &&
      std::memcmp(&cached_descriptor, &descriptor, sizeof(descriptor)) == 0 &&
      !retry.due(ota_verification_now_ms())) return cached;
  attempted = true; cached_running = running; cached_boot = boot;
  cached_state = state; cached_state_result = state_result; cached = {};
  cached_target_state = target_state; cached_target_state_result = target_state_result;
  cached_descriptor = descriptor; cached_descriptor_result = descriptor_result;
  cached_generation = generation;
  const esp_partition_t *target = nullptr;
  bool retryable = false;
  if (!find_bootstrap_rescue_target(target, cached.error, false, &retryable)) target = nullptr;
  retry.record(retryable, ota_verification_now_ms());
  if (target) {
    esp_app_desc_t app{};
    if (esp_ota_get_partition_description(target, &app) != ESP_OK) {
      cached.error = "ota_rescue_identity_unavailable";
      return cached;
    }
    cached.available = cached.verified = cached.protected_target = true;
    cached.label = target->label;
    cached.address = target->address;
    cached.size = target->size;
    cached.project_name = "lunaros-bootstrap";
    static constexpr char hex[] = "0123456789abcdef";
    for (const uint8_t byte : app.app_elf_sha256) {
      cached.app_elf_sha256 += hex[byte >> 4];
      cached.app_elf_sha256 += hex[byte & 15];
    }
  }
#endif
  return cached;
}
}
