#include "lunaros_ota_lifecycle.h"
#include "device_api/device_api_context.h"
#include "ota_payload.h"
#include "lunaredu_ota.h"
#include "lunaredu_ota_recovery.h"
#include <cstring>
#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#include "system/boot_guard.h"
#include <esp_ota_ops.h>
#endif

namespace lunaredu::lunaros {

namespace {

void fill_partition_payload(JsonObject payload, const char *label_key,
                            const char *address_key, const char *size_key,
                            const OtaPartitionSnapshot &partition) {
  payload[label_key] = partition.label;
  payload[address_key] = partition.address;
  payload[size_key] = partition.size;
}

} // namespace

void fill_ota_status_payload([[maybe_unused]] DeviceApiContext &device, JsonObject payload) {
  const OtaServiceStatus ota = ota_service_status_snapshot();
  const auto metadata = ota_writer_metadata();

  payload["ok"] = true;
  payload["has_result"] = ota.has_result;
  payload["success"] = ota.success;
  payload["upload_active"] = ota.upload_active;
  payload["pull_queued"] = ota.pull_queued;
  payload["pull_active"] = ota.pull_active;
  payload["pull_job_id"] = ota.pull_job_id;
  payload["pull_bytes_downloaded"] = ota.pull_bytes_downloaded;
  payload["pull_total_bytes"] = ota.pull_total_bytes;
  payload["pull_progress_percent"] = ota.pull_progress_percent;
  payload["message"] = ota.message.c_str();
  payload["restart_pending"] = ota.restart_pending;
  payload["rollback_enabled"] = ota.rollback_enabled;
  payload["signature_verification_enabled"] =
      ota.signature_verification_enabled;
  payload["anti_rollback_enabled"] = ota.anti_rollback_enabled;
  payload["flash_encryption_enabled"] = ota.flash_encryption_enabled;
  payload["pending_verify"] = ota.pending_verify;
  payload["boot_confirmed"] = ota.boot_confirmed;
  using Health = BootHealthObservation;
  const auto observation = device.boot_health_observation().bits;
  auto health = payload["boot_health"].to<JsonObject>();
  health["observed"] = (observation & Health::Observed) != 0;
  health["blocked_mask"] = observation & ~Health::Observed;
  auto blockers = health["blockers"].to<JsonArray>();
  for (const auto &item : {std::pair{Health::Application, "application_not_ready"},
       {Health::Io, "io_not_progressing"}, {Health::RequiredServices, "required_services_not_ready"},
       {Health::Reachability, "network_unreachable"}, {Health::Platform, "network_platform_not_ready"},
       {Health::DeviceApi, "device_api_not_ready"}, {Health::Bootstrap, "bootstrap_incomplete"}})
    if (observation & item.first) blockers.add(item.second);
  payload["boot_confirm_timeout_ms"] = ota.boot_confirm_timeout_ms;
  payload["boot_confirm_remaining_ms"] = ota.boot_confirm_remaining_ms;
  payload["running_image_state"] = ota.running_image_state;
  auto install = payload["installation"].to<JsonObject>();
  install["id"] = ota.install_id;
  install["target_address"] = ota.install_target_address;
  install["phase"] = ota.install_phase;
  install["sha256"] = ota.install_sha256;
  install["app_elf_sha256"] = ota.install_elf_sha256;
  install["persisted_bytes"] = ota.persisted_bytes;
  install["error"] = ota.install_error;
  install["journal_error"] = ota.journal_error;
  install["reset_reason"] = ota.reset_reason;
  const auto update = ota_service_available_update_snapshot();
  JsonObject available = payload["available_update"].to<JsonObject>();
  available["available"] = update.available;
  available["mandatory"] = update.mandatory;
  available["version"] = update.version.c_str();
  available["summary"] = update.summary.c_str();
  available["changelog"] = update.changelog.c_str();

  payload["project_name"] = metadata.project_name;
  payload["version"] = metadata.version;
  auto boot_update = payload["boot_update"].to<JsonObject>();
  boot_update["protocol_version"] = 1;
  boot_update["upload_operation"] = "boot.upload";
  boot_update["allowed"] = metadata.project_name != "lunaros-bootstrap";
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  boot_update["factory_address"] = 0x20000;
  boot_update["factory_bytes"] = 0x200000;
#ifdef ESP_PLATFORM
  const auto *factory = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
      ESP_PARTITION_SUBTYPE_APP_FACTORY,"recovery");
  esp_app_desc_t app{};
  if (factory && esp_ota_get_partition_description(factory,&app)==ESP_OK) {
    boot_update["factory_project"] = std::string(app.project_name,strnlen(app.project_name,sizeof(app.project_name)));
    boot_update["factory_version"] = std::string(app.version,strnlen(app.version,sizeof(app.version)));
    std::string elf;
    constexpr char hex[] = "0123456789abcdef";
    for (uint8_t byte : app.app_elf_sha256) { elf += hex[byte>>4]; elf += hex[byte&15]; }
    boot_update["factory_app_elf_sha256"] = elf;
  }
#endif
#endif
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  // One persistent snapshot per response: readiness and phase must refer to
  // the same generation, and recovery does not need the dual-slot machinery.
  const auto recovery = ota_recovery_snapshot();
  payload["deployment_mode"] = "recovery";
  payload["layout_id"] = ota_service_deployment_snapshot().layout_id;
  payload["firmware_role"] = recovery.recovery_running ? "recovery" : "business";
  auto delivery = payload["deployment"].to<JsonObject>();
  delivery["ready"] = recovery.ready;
  delivery["error"] = recovery.error;
  delivery["recovery_required"] = !recovery.recovery_running;
  delivery["previous_business_retained"] = false;
  // Ready requires the exact v3 partition table. Unavailable layouts report
  // no admitted capacity instead of presenting configured sizes as usable.
  delivery["business_slot_bytes"] = recovery.ready ? 0x600000 : 0;
  delivery["filesystem_bytes"] = recovery.ready ? ota_service_deployment_snapshot().filesystem_bytes : 0;
  auto state = payload["recovery"].to<JsonObject>();
  state["ready"] = recovery.ready;
  state["phase"] = recovery.phase;
  state["generation"] = recovery.generation;
  state["image_bytes"] = recovery.image_bytes;
  state["error"] = recovery.error;
  state["protocol_version"] = 1;
#else
  const auto deployment = ota_service_deployment_snapshot();
  payload["deployment_mode"] = deployment.mode;
  payload["layout_id"] = deployment.layout_id;
  payload["firmware_role"] = deployment.firmware_role;
  auto delivery = payload["deployment"].to<JsonObject>();
  delivery["ready"] = deployment.ready;
  delivery["error"] = deployment.error;
  delivery["recovery_required"] = metadata.project_name != "lunaros-bootstrap";
  delivery["previous_business_retained"] = deployment.previous_business_retained;
  delivery["business_slot_bytes"] = deployment.business_slot_bytes;
  delivery["filesystem_bytes"] = deployment.filesystem_bytes;
#endif
  payload["build_date"] = metadata.build_date;
  payload["build_time"] = metadata.build_time;
  payload["app_elf_sha256"] = metadata.app_elf_sha256;
#if !CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  // v3 has a permanent factory and its own recovery/deployment projection.
  // The legacy alternate-slot Bootstrap probe does not describe that layout.
  const auto rescue = ota_service_recovery_target_snapshot();
  auto rescue_target = payload["recovery_target"].to<JsonObject>();
  rescue_target["available"] = rescue.available;
  rescue_target["verified"] = rescue.verified;
  rescue_target["protected"] = rescue.protected_target;
  rescue_target["label"] = rescue.label;
  rescue_target["address"] = rescue.address;
  rescue_target["size"] = rescue.size;
  rescue_target["project_name"] = rescue.project_name;
  rescue_target["app_elf_sha256"] = rescue.app_elf_sha256;
  rescue_target["error"] = rescue.error;
#endif
#ifdef ESP_PLATFORM
  const auto guard = system_boot_guard_snapshot();
  auto boot_guard = payload["boot_guard"].to<JsonObject>();
  boot_guard["enabled"] = guard.enabled;
  boot_guard["watchdog_active"] = guard.watchdog_active;
  boot_guard["rescue_requested"] = guard.rescue_requested;
  boot_guard["consecutive_failures"] = guard.consecutive_failures;
  boot_guard["rescue_attempts"] = guard.rescue_attempts;
  boot_guard["reset_reason"] = guard.reset_reason;
  boot_guard["error"] = guard.error;
  boot_guard["configured_timeout_ms"] = guard.configured_timeout_ms;
  boot_guard["continuous_health_ms"] = guard.continuous_health_ms;
  boot_guard["health_committed"] = guard.health_committed;
  boot_guard["health_interruptions"] = guard.health_interruptions;
  boot_guard["last_blocked_mask"] = guard.last_blocked_mask;
  boot_guard["last_interruption_ms"] = guard.last_interruption_ms;
  boot_guard["interrupted_health_ms"] = guard.interrupted_health_ms;
#endif

  fill_partition_payload(payload, "running_label", "running_address",
                         "running_size", metadata.running);
  fill_partition_payload(payload, "boot_label", "boot_address", "boot_size",
                         metadata.boot);
  fill_partition_payload(payload, "next_label", "next_address", "next_size",
                         metadata.next);
}

} // namespace lunaredu::lunaros
