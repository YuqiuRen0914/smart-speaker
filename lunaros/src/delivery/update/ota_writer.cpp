#include "delivery/update/ota_writer.h"

#include <sdkconfig.h>
#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#include "delivery/update/ota_image_policy.h"

namespace lunaredu::lunaros {
namespace {

esp_ota_handle_t g_handle = 0;
const esp_partition_t *g_partition = nullptr;
size_t g_expected_size = 0;
size_t g_written_size = 0;

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
  g_handle = 0;
  g_partition = nullptr;
  g_expected_size = 0;
  g_written_size = 0;
}

OtaWriterPartitionInfo partition_info(const esp_partition_t *partition) {
  OtaWriterPartitionInfo info;
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

OtaWriterMetadata ota_writer_metadata() {
  OtaWriterMetadata metadata;
  const esp_app_desc_t *app = esp_app_get_description();
  if (app != nullptr) {
    metadata.project_name = app->project_name;
    metadata.version = app->version;
    metadata.build_date = app->date;
    metadata.build_time = app->time;
  }
  metadata.running = partition_info(esp_ota_get_running_partition());
  metadata.boot = partition_info(esp_ota_get_boot_partition());
  metadata.next = partition_info(esp_ota_get_next_update_partition(nullptr));
  return metadata;
}

bool ota_writer_confirm_image(std::string &error) {
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
}

bool ota_writer_rollback_and_reboot(std::string &error) {
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
  const esp_err_t result = esp_ota_mark_app_invalid_rollback_and_reboot();
  error = esp_err_to_name(result);
  return false;
#else
  error = "rollback_not_enabled";
  return false;
#endif
}

bool ota_writer_begin(size_t image_size, std::string &error) {
  g_partition = esp_ota_get_next_update_partition(nullptr);
  if (g_partition == nullptr) {
    error = "ota_partition_not_found";
    clear_session();
    return false;
  }
  if (image_size == 0 || image_size > g_partition->size) {
    error = "ota_image_exceeds_partition";
    clear_session();
    return false;
  }
  const esp_err_t result =
      esp_ota_begin(g_partition, image_size, &g_handle);
  if (result == ESP_OK) {
    g_expected_size = image_size;
    g_written_size = 0;
    error.clear();
    return true;
  }
  error = esp_err_to_name(result);
  clear_session();
  return false;
}

bool ota_writer_write(const uint8_t *data, size_t size,
                           std::string &error) {
  if (g_handle == 0 || size > g_expected_size - g_written_size) {
    error = "ota_image_size_overflow";
    return false;
  }
  const esp_err_t result = esp_ota_write(g_handle, data, size);
  if (result == ESP_OK) {
    g_written_size += size;
    error.clear();
    return true;
  }
  error = esp_err_to_name(result);
  return false;
}

bool ota_writer_complete(std::string &error) {
  if (g_handle == 0 || g_written_size != g_expected_size) {
    error = "ota_image_size_mismatch";
    ota_writer_abort();
    return false;
  }
  esp_err_t result = esp_ota_end(g_handle);
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
  if (!ota_candidate_identity_allowed(running_identity, candidate_identity,
                                      error,
                                      CONFIG_LUNAREDU_SYSTEM_OTA_BOOTSTRAP_PROJECTS)) {
    clear_session();
    return false;
  }
  result = esp_ota_set_boot_partition(g_partition);
  clear_session();
  if (result == ESP_OK) {
    error.clear();
    return true;
  }
  error = esp_err_to_name(result);
  return false;
}

void ota_writer_abort() {
  if (g_handle != 0) {
    (void)esp_ota_abort(g_handle);
  }
  clear_session();
}

void ota_writer_cooperative_yield() { vTaskDelay(pdMS_TO_TICKS(1)); }

} // namespace lunaredu::lunaros
