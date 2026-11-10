#include "device_api/projections/ota_payload.h"
#include "lunaredu_lunaros/delivery/update.h"

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

void fill_ota_status_payload(JsonObject payload) {
  const OtaServiceStatus ota = ota_service_status_snapshot();
  const auto metadata = ota_service_image_metadata_snapshot();

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
  payload["boot_confirm_timeout_ms"] = ota.boot_confirm_timeout_ms;
  payload["boot_confirm_remaining_ms"] = ota.boot_confirm_remaining_ms;
  payload["running_image_state"] = ota.running_image_state;
  const auto update = ota_service_available_update_snapshot();
  JsonObject available = payload["available_update"].to<JsonObject>();
  available["available"] = update.available;
  available["mandatory"] = update.mandatory;
  available["version"] = update.version.c_str();
  available["summary"] = update.summary.c_str();
  available["changelog"] = update.changelog.c_str();

  payload["project_name"] = metadata.project_name;
  payload["version"] = metadata.version;
  payload["build_date"] = metadata.build_date;
  payload["build_time"] = metadata.build_time;

  fill_partition_payload(payload, "running_label", "running_address",
                         "running_size", metadata.running);
  fill_partition_payload(payload, "boot_label", "boot_address", "boot_size",
                         metadata.boot);
  fill_partition_payload(payload, "next_label", "next_address", "next_size",
                         metadata.next);
}

} // namespace lunaredu::lunaros
