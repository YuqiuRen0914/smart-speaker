#include "ota_sdk_verification.h"
#include "ota_writer.h"

#include <sdkconfig.h>
#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
#include <esp_image_format.h>
#endif
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>
#include <algorithm>

#include "ota_image_policy.h"
#include "ota_rescue.h"
#include "lunaredu_ota.h"
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
#include "recovery_internal.h"
#endif

namespace lunaredu::lunaros {
namespace {

esp_ota_handle_t g_handle = 0;
const esp_partition_t *g_partition = nullptr;
size_t g_expected_size = 0;
size_t g_written_size = 0;
std::string g_last_candidate_elf_sha256;
bool g_last_candidate_selected = false;
OtaWriterPrecommitCallback g_precommit = nullptr;
bool g_boot_update = false;
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
bool g_factory_write = false;
size_t g_raw_offset = 0, g_tail_size = 0;
alignas(16) uint8_t g_tail[16];
bool raw_write(const uint8_t *data, size_t size, std::string &error) {
  while (size) {
    if (g_raw_offset % 4096 == 0) {
      const auto result = esp_partition_erase_range(g_partition, g_raw_offset, 4096);
      if (result != ESP_OK) { error = esp_err_to_name(result); return false; }
    }
    const size_t amount = std::min(size, size_t{4096} - g_raw_offset % 4096);
    const auto result = esp_partition_write(g_partition, g_raw_offset, data, amount);
    if (result != ESP_OK) { error = esp_err_to_name(result); return false; }
    data += amount; size -= amount; g_raw_offset += amount;
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return true;
}
bool raw_append(const uint8_t *data, size_t size, std::string &error) {
  if (g_tail_size) {
    const size_t amount = std::min(size, sizeof(g_tail) - g_tail_size);
    std::memcpy(g_tail + g_tail_size, data, amount);
    g_tail_size += amount; data += amount; size -= amount;
    if (g_tail_size == sizeof(g_tail)) {
      if (!raw_write(g_tail, sizeof(g_tail), error)) return false;
      g_tail_size = 0;
    }
  }
  const size_t aligned = size & ~size_t{15};
  if (aligned && !raw_write(data, aligned, error)) return false;
  data += aligned; size -= aligned;
  if (size) { std::memcpy(g_tail, data, size); g_tail_size = size; }
  return true;
}
#endif

const char *image_state_name(esp_ota_img_states_t state) {
  switch (state) {
  case ESP_OTA_IMG_NEW:
    return "new";
  case ESP_OTA_IMG_PENDING_VERIFY:
    return "pending_verify";
  case ESP_OTA_IMG_VALID:
    return "valid";
  case ESP_OTA_IMG_INVALID:
    return "invalid";
  case ESP_OTA_IMG_ABORTED:
    return "aborted";
  case ESP_OTA_IMG_UNDEFINED:
  default:
    return "undefined";
  }
}

void clear_session() {
  ota_sdk_image_write_end();
  g_handle = 0;
  g_partition = nullptr;
  g_expected_size = 0;
  g_written_size = 0;
  g_precommit = nullptr;
  g_boot_update = false;
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  g_factory_write = false;
  g_raw_offset = g_tail_size = 0;
#endif
}

OtaPartitionSnapshot partition_info(const esp_partition_t *partition) {
  OtaPartitionSnapshot info;
  if (partition != nullptr) {
    info.label = partition->label;
    info.address = partition->address;
    info.size = partition->size;
  }
  return info;
}

} // namespace

OtaWriterImageStatus ota_writer_image_status() {
  OtaWriterImageStatus status;
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
  status.rollback_enabled = true;
#endif
#if CONFIG_SECURE_BOOT || CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT
  status.signature_verification_enabled = true;
#endif
#if CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
  status.anti_rollback_enabled = true;
#endif
#if CONFIG_SECURE_FLASH_ENC_ENABLED
  status.flash_encryption_enabled = true;
#endif
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running == nullptr) {
    status.state = "unavailable";
    return status;
  }
  if (running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
    status.state = "factory";
    return status;
  }

  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t result = esp_ota_get_state_partition(running, &state);
  if (result == ESP_ERR_NOT_FOUND) {
    status.state = "undefined";
    return status;
  }
  if (result != ESP_OK) {
    status.state = "state_error";
    status.error = esp_err_to_name(result);
    return status;
  }
  status.state = image_state_name(state);
  status.pending_verify = state == ESP_OTA_IMG_PENDING_VERIFY;
  return status;
}

OtaImageMetadataSnapshot ota_writer_metadata() {
  OtaImageMetadataSnapshot metadata;
  const esp_app_desc_t *app = esp_app_get_description();
  if (app != nullptr) {
    metadata.project_name = app->project_name;
    metadata.version = app->version;
    metadata.build_date = app->date;
    metadata.build_time = app->time;
    static constexpr char hex[] = "0123456789abcdef";
    metadata.app_elf_sha256.reserve(64);
    for (uint8_t byte : app->app_elf_sha256) {
      metadata.app_elf_sha256 += hex[byte >> 4];
      metadata.app_elf_sha256 += hex[byte & 15];
    }
  }
  metadata.running = partition_info(esp_ota_get_running_partition());
  metadata.boot = partition_info(esp_ota_get_boot_partition());
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  const auto recovery=ota_recovery_snapshot();
  if(recovery.ready&&recovery.recovery_running){
    std::string error;
    metadata.next = partition_info(ota_recovery_write_target(error));
  }
#else
  metadata.next = partition_info(esp_ota_get_next_update_partition(nullptr));
#endif
  return metadata;
}

bool ota_writer_select_rescue(std::string &error) {
  if (g_handle != 0) { error = "ota_rescue_install_active"; return false; }
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  if (g_factory_write) { error = "ota_rescue_install_active"; return false; }
#endif
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  return ota_recovery_select_factory_for_rescue(error);
#else
  return ota_select_bootstrap_rescue(error);
#endif
}

bool ota_writer_confirm_image(std::string &error) {
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  return ota_recovery_confirm_business(error);
#else
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
  const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
  if (result == ESP_OK) {
    error.clear();
    return true;
  }
  error = esp_err_to_name(result);
  return false;
#else
  error = "rollback_not_enabled";
  return false;
#endif
#endif
}

bool ota_writer_rollback_and_reboot(std::string &error) {
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
  const esp_err_t result = ota_sdk_rollback_and_reboot();
  error = esp_err_to_name(result);
  return false;
#else
  error = "rollback_not_enabled";
  return false;
#endif
}

bool ota_writer_begin(size_t image_size, std::string &error,
                      [[maybe_unused]] bool maintenance_authorized, OtaWriterPrecommitCallback precommit,
                      bool bootstrap_update) {
  const auto *current = esp_app_get_description();
  const bool running_boot = current && !std::memcmp(current->project_name, "lunaros-bootstrap", sizeof("lunaros-bootstrap"));
  if (!current || bootstrap_update == running_boot) {
    error = running_boot ? "ota_boot_requires_application" : "ota_recovery_required";
    return false;
  }
  g_last_candidate_elf_sha256.clear();
  g_last_candidate_selected = false;
#if CONFIG_LUNAREDU_OTA_DUAL_SLOT_LAYOUT
  if (!ota_service_initialize_deployment(error)) { clear_session(); return false; }
#endif
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  g_partition = bootstrap_update ? ota_recovery_boot_write_target(image_size,error)
                                 : ota_recovery_write_target(error);
#else
  g_partition = esp_ota_get_next_update_partition(nullptr);
#endif
  if (g_partition == nullptr) {
    if (error.empty()) error = "ota_partition_not_found";
    clear_session();
    return false;
  }
#if !CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  if (bootstrap_update && !ota_bootstrap_update_admitted(error)) { clear_session(); return false; }
  // Preserve a verified legacy recovery image. Only Bootstrap itself may
  // install into the other business slot after an authorized handoff.
  std::string rescue_error;
  constexpr char bootstrap_project[] = "lunaros-bootstrap";
  const auto *running_app = esp_app_get_description();
  esp_app_desc_t existing_target{};
  const esp_err_t target_description = esp_ota_get_partition_description(g_partition, &existing_target);
  if (!running_app || (!bootstrap_update && target_description != ESP_OK && target_description != ESP_ERR_NOT_FOUND)) {
    error = "recovery_target_unavailable";
    clear_session();
    return false;
  }
  const bool running_bootstrap = std::memcmp(running_app->project_name,
      bootstrap_project, sizeof(bootstrap_project)) == 0;
  if (!bootstrap_update && !running_bootstrap && target_description == ESP_OK &&
      std::memcmp(existing_target.project_name, bootstrap_project, sizeof(bootstrap_project)) == 0) {
    error = ota_bootstrap_rescue_target(rescue_error) == g_partition
        ? "recovery_handoff_required" : "recovery_target_unavailable";
    clear_session();
    return false;
  }
#endif
#if CONFIG_LUNAREDU_OTA_DUAL_SLOT_LAYOUT
  const auto *running = esp_ota_get_running_partition();
  const auto *expected = running ? esp_partition_find_first(ESP_PARTITION_TYPE_APP,
      running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0
          ? static_cast<esp_partition_subtype_t>(17) : ESP_PARTITION_SUBTYPE_APP_OTA_0,
      nullptr) : nullptr;
  if (g_partition != expected || g_partition == running) {
    error = "ota_deployment_target_mismatch";
    clear_session();
    return false;
  }
#endif
  if (image_size == 0 || image_size > g_partition->size) {
    error = "ota_image_exceeds_partition";
    clear_session();
    return false;
  }
  ota_sdk_image_write_begin();
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  if (!bootstrap_update && !ota_recovery_begin_write(image_size, error)) {
    clear_session();
    return false;
  }
  if (bootstrap_update && g_partition->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
    g_factory_write = g_boot_update = true;
    g_expected_size = image_size;
    g_written_size = g_raw_offset = g_tail_size = 0;
    g_precommit = precommit;
    error.clear(); return true;
  }
#endif
  const esp_err_t result =
      esp_ota_begin(g_partition, OTA_WITH_SEQUENTIAL_WRITES, &g_handle);
  if (result == ESP_OK) {
    g_expected_size = image_size;
    g_written_size = 0;
    g_precommit = precommit;
    g_boot_update = bootstrap_update;
    error.clear();
    return true;
  }
  error = esp_err_to_name(result);
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  ota_recovery_write_failed();
#endif
  clear_session();
  return false;
}

bool ota_writer_write(const uint8_t *data, size_t size,
                           std::string &error) {
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  if (g_factory_write) {
    if (size > g_expected_size - g_written_size) { error = "ota_image_size_overflow"; return false; }
    if (!raw_append(data, size, error)) return false;
    g_written_size += size; error.clear(); return true;
  }
#endif
  if (g_handle == 0 || size > g_expected_size - g_written_size) {
    error = "ota_image_size_overflow";
    return false;
  }
  const esp_err_t result = esp_ota_write(g_handle, data, size);
  if (result == ESP_OK) {
    g_written_size += size;
    // Sequential erase bounds flash work to this write, not the whole image.
    ota_writer_cooperative_yield();
    error.clear();
    return true;
  }
  error = esp_err_to_name(result);
  return false;
}

bool ota_writer_complete(std::string &error) {
  const bool raw =
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
      g_factory_write;
#else
      false;
#endif
  if ((!raw && g_handle == 0) || g_written_size != g_expected_size) {
    error = "ota_image_size_mismatch";
    ota_writer_abort();
    return false;
  }
  esp_err_t result = ESP_OK;
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  if (raw) {
    if (g_tail_size) {
      std::memset(g_tail + g_tail_size, 0xff, sizeof(g_tail) - g_tail_size);
      if (!raw_write(g_tail, sizeof(g_tail), error)) { clear_session(); return false; }
    }
    esp_image_metadata_t image{};
    const esp_partition_pos_t pos{g_partition->address,g_partition->size};
    result = ota_sdk_verify_image(ESP_IMAGE_VERIFY, &pos, &image);
  } else
#endif
  result = ota_sdk_end(g_handle);
  if (result != ESP_OK) {
    error = esp_err_to_name(result);
    clear_session();
    return false;
  }

  esp_app_desc_t candidate = {};
  result = esp_ota_get_partition_description(g_partition, &candidate);
  if (result != ESP_OK) {
    error = esp_err_to_name(result);
    clear_session();
    return false;
  }
  static constexpr char hex[] = "0123456789abcdef";
  g_last_candidate_elf_sha256.clear();
  for (uint8_t byte : candidate.app_elf_sha256) {
    g_last_candidate_elf_sha256 += hex[byte >> 4];
    g_last_candidate_elf_sha256 += hex[byte & 15];
  }
  const esp_app_desc_t *running = esp_app_get_description();
  if (running == nullptr) {
    error = "ota_running_identity_unavailable";
    clear_session();
    return false;
  }
  const auto text = [](const char *value, size_t capacity) {
    return std::string(value, strnlen(value, capacity));
  };
  const OtaCandidateIdentity running_identity = {
      text(running->project_name, sizeof(running->project_name)),
      text(running->version, sizeof(running->version)), running->secure_version};
  const OtaCandidateIdentity candidate_identity = {
      text(candidate.project_name, sizeof(candidate.project_name)),
      text(candidate.version, sizeof(candidate.version)), candidate.secure_version};
  if (g_boot_update && candidate_identity.project_name != "lunaros-bootstrap") {
    error = "ota_boot_project_required"; clear_session(); return false;
  }
  if (candidate_identity.project_name == "lunaros-bootstrap") {
    const bool same = running->secure_version == candidate.secure_version &&
        !std::memcmp(running->app_elf_sha256,candidate.app_elf_sha256,sizeof(candidate.app_elf_sha256));
    if (!g_boot_update) { error = "ota_application_image_required"; clear_session(); return false; }
    if (!ota_boot_candidate_identity_allowed(running_identity, candidate_identity, error, same)) {
      clear_session();return false;
    }
  }
  if (!g_boot_update && !ota_candidate_identity_allowed(running_identity, candidate_identity, error)) {
    clear_session();
    return false;
  }
  if (g_precommit) g_precommit(g_last_candidate_elf_sha256, g_written_size, g_partition->address);
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  if (!(g_boot_update ? ota_recovery_finish_boot_write(g_partition,error)
                     : ota_recovery_finish_write(g_partition, error))) {
    clear_session();
    return false;
  }
#endif
  if (raw && running_identity.project_name != "lunaros-bootstrap") {
    // Product repaired factory in place. Keep this confirmed product selected
    // and alive; a separate explicit handoff may try the new Boot later.
    clear_session(); error.clear(); return true;
  }
  result = ota_sdk_set_boot_partition(g_partition);
  clear_session();
  if (result == ESP_OK) {
    g_last_candidate_selected = true;
    error.clear();
    return true;
  }
  error = esp_err_to_name(result);
  return false;
}

std::string ota_writer_last_candidate_elf_sha256() { return g_last_candidate_elf_sha256; }
bool ota_writer_last_candidate_selected() { return g_last_candidate_selected; }

void ota_writer_abort() {
  if (g_handle != 0) {
    (void)esp_ota_abort(g_handle);
  }
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  ota_recovery_write_failed();
#endif
  clear_session();
}

void ota_writer_cooperative_yield() { vTaskDelay(pdMS_TO_TICKS(1)); }

} // namespace lunaredu::lunaros
