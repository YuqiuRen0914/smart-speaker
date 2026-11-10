#include "lunaredu_lunaros/delivery/update.h"

#include <sdkconfig.h>

#include <algorithm>
#include <mutex>

#include "lunaredu_lunaros/model/device_model.h"
#include "lunaredu_lunaros/kernel/log.h"
#include "kernel/system_primitives.h"
#include "delivery/update/ota_status_internal.h"
#include "delivery/update/ota_version.h"
#include "delivery/update/ota_writer.h"

#ifndef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#define CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 0
#endif

#ifndef CONFIG_LUNAREDU_SYSTEM_OTA_BOOT_CONFIRM_TIMEOUT_MS
#define CONFIG_LUNAREDU_SYSTEM_OTA_BOOT_CONFIRM_TIMEOUT_MS 30000
#endif

namespace lunaredu::lunaros {

namespace {

const DeviceCapabilityDescriptor kOtaCapability = []() {
  DeviceCapabilityDescriptor descriptor;
  descriptor.id = "ota_service";
  return descriptor;
}();

OtaServiceStatus g_ota_status;
OtaAvailableUpdate g_available_update;
bool g_ota_upload_active = false;
std::recursive_mutex g_ota_mutex;
int64_t g_boot_confirm_deadline_ms = 0;
OtaServiceLifecycleCallback g_lifecycle_callback = nullptr;
void *g_lifecycle_context = nullptr;

int64_t now_ms() {
  return lunaredu::lunaros::internal::system_monotonic_ms();
}

void refresh_boot_confirmation_remaining_locked() {
  if (!g_ota_status.pending_verify || g_boot_confirm_deadline_ms <= 0) {
    g_ota_status.boot_confirm_remaining_ms = 0;
    return;
  }
  const int64_t remaining = g_boot_confirm_deadline_ms - now_ms();
  g_ota_status.boot_confirm_remaining_ms =
      remaining <= 0
          ? 0
          : static_cast<uint32_t>(
                std::min<int64_t>(remaining, static_cast<int64_t>(UINT32_MAX)));
}

void inspect_running_image_locked() {
  const auto image_status = lunaredu::lunaros::ota_writer_image_status();
  g_ota_status.rollback_enabled = image_status.rollback_enabled;
  g_ota_status.signature_verification_enabled =
      image_status.signature_verification_enabled;
  g_ota_status.anti_rollback_enabled = image_status.anti_rollback_enabled;
  g_ota_status.flash_encryption_enabled = image_status.flash_encryption_enabled;
  g_ota_status.boot_confirm_timeout_ms =
      CONFIG_LUNAREDU_SYSTEM_OTA_BOOT_CONFIRM_TIMEOUT_MS;
  g_ota_status.pending_verify = image_status.pending_verify;
  g_ota_status.boot_confirmed = !image_status.pending_verify;
  g_boot_confirm_deadline_ms = 0;
  g_ota_status.running_image_state = image_status.state;
  if (!image_status.error.empty()) {
    g_ota_status.running_image_state += ":" + image_status.error;
  }

  if (image_status.pending_verify) {
    g_boot_confirm_deadline_ms =
        now_ms() + CONFIG_LUNAREDU_SYSTEM_OTA_BOOT_CONFIRM_TIMEOUT_MS;
    g_ota_status.message = "boot_pending_verify";
    refresh_boot_confirmation_remaining_locked();
  }
}

void set_ota_upload_active_locked(bool active) {
  g_ota_upload_active = active;
  g_ota_status.upload_active = active;
}

void clear_ota_session_locked() {
  set_ota_upload_active_locked(false);
}

void notify_lifecycle_locked(OtaServiceLifecycleEvent event) {
  if (g_lifecycle_callback != nullptr) {
    g_lifecycle_callback(event, g_lifecycle_context);
  }
}

} // 匿名命名空间

const DeviceCapabilityDescriptor &ota_capability_descriptor() {
  return kOtaCapability;
}

void setup_ota_service() {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  g_ota_status = OtaServiceStatus();
  g_available_update = {};
  clear_ota_session_locked();
  inspect_running_image_locked();
  if (!activate_device_capability(ota_capability_descriptor())) {
    LOGW("system.ota", "failed to register capability id=%s",
         ota_capability_descriptor().id ? ota_capability_descriptor().id
                                        : "(null)");
  }
}

void ota_service_set_lifecycle_callback(OtaServiceLifecycleCallback callback,
                                        void *context) {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  g_lifecycle_callback = callback;
  g_lifecycle_context = context;
}

void ota_service_clear_lifecycle_callback() {
  ota_service_set_lifecycle_callback(nullptr, nullptr);
}

void tick_ota_service() {
  bool restart_pending = false;
  bool rollback_due = false;
  {
    std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
    refresh_boot_confirmation_remaining_locked();
    rollback_due = g_ota_status.pending_verify &&
                   g_boot_confirm_deadline_ms > 0 &&
                   now_ms() >= g_boot_confirm_deadline_ms;
    if (rollback_due) {
      g_ota_status.message = "boot_confirmation_timeout";
    }
    restart_pending = g_ota_status.restart_pending;
  }

  if (rollback_due) {
    (void)ota_service_rollback_running_image("boot_confirmation_timeout");
    return;
  }

  if (!restart_pending) {
    return;
  }
  lunaredu::lunaros::internal::system_restart_after_delay(200);
}

OtaServiceStatus ota_service_status_snapshot() {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  refresh_boot_confirmation_remaining_locked();
  return g_ota_status;
}

bool ota_service_boot_pending() {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  return g_ota_status.pending_verify;
}

bool ota_service_upload_active() {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  return g_ota_upload_active;
}

bool ota_service_restart_pending() {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  return g_ota_status.restart_pending;
}

void ota_service_request_restart() {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  notify_lifecycle_locked(OtaServiceLifecycleEvent::RestartRequested);
  g_ota_status.has_result = true;
  g_ota_status.success = true;
  g_ota_status.restart_pending = true;
  if (g_ota_status.message.empty() ||
      g_ota_status.message == "ota_restart_pending") {
    g_ota_status.message = "restart_requested";
  }
}

bool ota_service_confirm_running_image() {
  bool pending_verify = false;
  {
    std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
    pending_verify = g_ota_status.pending_verify;
    if (!pending_verify) {
      g_ota_status.boot_confirmed = true;
      g_ota_status.boot_confirm_remaining_ms = 0;
      g_boot_confirm_deadline_ms = 0;
      return true;
    }
  }

  std::string writer_error;
  const bool confirmed =
      lunaredu::lunaros::ota_writer_confirm_image(writer_error);
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  if (!confirmed) {
    g_ota_status.message = "boot_confirm_failed:" + writer_error;
    return false;
  }
  g_ota_status.pending_verify = false;
  g_ota_status.boot_confirmed = true;
  g_ota_status.boot_confirm_remaining_ms = 0;
  g_ota_status.running_image_state = "valid";
  g_ota_status.message = "boot_confirmed";
  g_boot_confirm_deadline_ms = 0;
  LOGI("system.ota", "running OTA image confirmed");
  return true;
}

OtaAvailableUpdate ota_service_available_update_snapshot() {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  return g_available_update;
}

void ota_service_set_available_update(const OtaAvailableUpdate &update) {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  g_available_update = update;
  g_available_update.version = internal::normalize_ota_release_version(update.version);
  if (!update.available) {
    g_available_update = {};
  }
}

bool ota_service_apply_available_update(std::string &error) {
  OtaAvailableUpdate update;
  {
    std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
    update = g_available_update;
  }
  if (!update.available || update.download_url.empty() ||
      update.package_sha256.empty() || update.package_size == 0) {
    error = "ota_update_unavailable";
    ota_service_report_failure(error.c_str());
    return false;
  }
  uint32_t job_id = 0;
  const bool queued = ota_service_queue_install_from_https(
      update.download_url, update.package_sha256, update.package_size,
      job_id, error);
  if (!queued) {
    ota_service_report_failure(error.c_str());
  }
  return queued;
}

OtaImageMetadataSnapshot ota_service_image_metadata_snapshot() {
  const auto metadata = lunaredu::lunaros::ota_writer_metadata();
  OtaImageMetadataSnapshot snapshot;
  snapshot.project_name = metadata.project_name;
  snapshot.version = metadata.version;
  snapshot.build_date = metadata.build_date;
  snapshot.build_time = metadata.build_time;
  snapshot.running = {metadata.running.label, metadata.running.address,
                      metadata.running.size};
  snapshot.boot = {metadata.boot.label, metadata.boot.address,
                   metadata.boot.size};
  snapshot.next = {metadata.next.label, metadata.next.address,
                   metadata.next.size};
  return snapshot;
}

bool ota_service_rollback_running_image(const char *reason) {
  {
    std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
    // Boot may fail before setup_ota_service; consult the bootloader directly.
    const auto image = ota_writer_image_status();
    g_ota_status.rollback_enabled = image.rollback_enabled;
    g_ota_status.pending_verify = image.pending_verify;
    if (!image.rollback_enabled) {
      g_ota_status.message = "rollback_not_enabled";
      return false;
    }
    if (!g_ota_status.pending_verify) {
      g_ota_status.message = "rollback_not_pending";
      return false;
    }
    g_ota_status.message =
        reason != nullptr && reason[0] != '\0' ? reason : "rollback_requested";
    g_ota_status.boot_confirm_remaining_ms = 0;
    g_boot_confirm_deadline_ms = 0;
  }

  LOGE("system.ota", "rejecting running OTA image reason=%s",
       reason != nullptr ? reason : "rollback_requested");
  std::string writer_error;
  (void)lunaredu::lunaros::ota_writer_rollback_and_reboot(writer_error);
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  g_ota_status.message = "rollback_failed:" + writer_error;
  return false;
}

bool ota_service_begin_upload(size_t image_size) {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  if (g_ota_upload_active) {
    g_ota_status.success = false;
    g_ota_status.has_result = true;
    g_ota_status.message = "ota_upload_in_progress";
    return false;
  }
  if (g_ota_status.restart_pending) {
    g_ota_status.success = false;
    g_ota_status.has_result = true;
    g_ota_status.message = "ota_restart_pending";
    return false;
  }

  notify_lifecycle_locked(OtaServiceLifecycleEvent::UploadStarting);

  g_ota_status.success = false;
  g_ota_status.has_result = false;
  g_ota_status.restart_pending = false;
  g_ota_status.message = "upload_started";
  g_ota_status.pull_bytes_downloaded = 0;
  g_ota_status.pull_total_bytes = image_size;
  g_ota_status.pull_progress_percent = 0;
  std::string writer_error;
  if (lunaredu::lunaros::ota_writer_begin(image_size, writer_error)) {
    set_ota_upload_active_locked(true);
    return true;
  }

  clear_ota_session_locked();
  g_ota_status.has_result = true;
  g_ota_status.message = writer_error;
  return false;
}

bool ota_service_write_upload_chunk(uint8_t *data, size_t size) {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  if (!g_ota_upload_active) {
    g_ota_status.message = "ota_upload_not_started";
    return false;
  }
  if (size > 0 && data == nullptr) {
    g_ota_status.message = "invalid_ota_chunk";
    return false;
  }
  if (size == 0) {
    return true;
  }

  std::string writer_error;
  if (lunaredu::lunaros::ota_writer_write(data, size, writer_error)) {
    if (g_ota_status.pull_total_bytes > 0) {
      g_ota_status.pull_bytes_downloaded = std::min(
          g_ota_status.pull_total_bytes,
          g_ota_status.pull_bytes_downloaded + size);
      g_ota_status.pull_progress_percent = static_cast<uint8_t>(
          (g_ota_status.pull_bytes_downloaded * 100U) /
          g_ota_status.pull_total_bytes);
    }
    return true;
  }

  g_ota_status.message = writer_error;
  return false;
}

bool ota_service_complete_upload() {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  if (!g_ota_upload_active) {
    g_ota_status.success = false;
    g_ota_status.has_result = true;
    g_ota_status.message = "ota_upload_not_started";
    return false;
  }

  std::string writer_error;
  const bool completed =
      lunaredu::lunaros::ota_writer_complete(writer_error);
  clear_ota_session_locked();

  if (completed) {
    g_ota_status.success = true;
    g_ota_status.has_result = true;
    g_ota_status.restart_pending = true;
    g_ota_status.message = "ota_success_restarting";
    notify_lifecycle_locked(OtaServiceLifecycleEvent::UploadCompleted);
    return true;
  }

  g_ota_status.success = false;
  g_ota_status.has_result = true;
  g_ota_status.message = writer_error;
  return false;
}

void ota_service_abort_upload() {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  if (g_ota_upload_active) {
    lunaredu::lunaros::ota_writer_abort();
  }
  clear_ota_session_locked();
  g_ota_status.success = false;
  g_ota_status.has_result = true;
  g_ota_status.restart_pending = false;
  g_ota_status.message = "upload_aborted";
  notify_lifecycle_locked(OtaServiceLifecycleEvent::UploadAborted);
}

void ota_service_report_pull_queued(uint32_t job_id) {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  g_ota_status.pull_job_id = job_id;
  g_ota_status.pull_queued = true;
  g_ota_status.pull_active = false;
  g_ota_status.has_result = false;
  g_ota_status.success = false;
  g_ota_status.message = "ota_pull_queued";
}

void ota_service_report_preparing(const char *message) {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  g_ota_status.pull_job_id = 0;
  g_ota_status.pull_queued = true;
  g_ota_status.pull_active = false;
  g_ota_status.has_result = false;
  g_ota_status.success = false;
  g_ota_status.restart_pending = false;
  g_ota_status.message = message != nullptr && message[0] != '\0'
                             ? message
                             : "ota_ticket_refresh_queued";
}

void ota_service_report_pull_started(uint32_t job_id) {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  g_ota_status.pull_job_id = job_id;
  g_ota_status.pull_queued = false;
  g_ota_status.pull_active = true;
  g_ota_status.has_result = false;
  g_ota_status.success = false;
  g_ota_status.message = "ota_pull_started";
}

void ota_service_report_pull_finished(uint32_t job_id) {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  if (g_ota_status.pull_job_id != job_id) {
    return;
  }
  g_ota_status.pull_queued = false;
  g_ota_status.pull_active = false;
}

void ota_service_report_failure(const char *message) {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  g_ota_status.pull_queued = false;
  g_ota_status.pull_active = false;
  g_ota_status.success = false;
  g_ota_status.has_result = true;
  g_ota_status.restart_pending = false;
  g_ota_status.message = message != nullptr && message[0] != '\0'
                             ? message
                             : "ota_source_failed";
}

bool ota_service_install_stream(const OtaFirmwareStream &stream) {
  if (stream.content_length == 0 || stream.read_chunk == nullptr) {
    std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
    g_ota_status.success = false;
    g_ota_status.has_result = true;
    g_ota_status.restart_pending = false;
    set_ota_upload_active_locked(false);
    g_ota_status.message = "empty_ota_stream";
    return false;
  }

  const OtaImageMetadataSnapshot image = ota_service_image_metadata_snapshot();
  if (image.next.size == 0 || stream.content_length > image.next.size) {
    ota_service_report_failure("ota_image_exceeds_partition");
    return false;
  }

  if (!ota_service_begin_upload(stream.content_length)) {
    return false;
  }

  size_t remaining = stream.content_length;
  size_t bytes_since_yield = 0;
  uint8_t buffer[1024];
  while (remaining > 0) {
    const size_t chunk_size =
        remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
    const int read_size =
        stream.read_chunk(buffer, chunk_size, stream.context);
    if (read_size <= 0 || static_cast<size_t>(read_size) > chunk_size) {
      const char *read_error = stream.read_error == nullptr
                                   ? nullptr
                                   : stream.read_error(stream.context);
      ota_service_abort_upload();
      ota_service_report_failure(read_error != nullptr && read_error[0] != '\0'
                                     ? read_error
                                     : "ota_read_failed");
      return false;
    }

    if (!ota_service_write_upload_chunk(buffer, static_cast<size_t>(read_size))) {
      const OtaServiceStatus failed = ota_service_status_snapshot();
      const std::string write_error = failed.message.empty()
                                          ? "ota_write_failed"
                                          : failed.message;
      ota_service_abort_upload();
      ota_service_report_failure(write_error.c_str());
      return false;
    }
    remaining -= static_cast<size_t>(read_size);
    bytes_since_yield += static_cast<size_t>(read_size);
    if (bytes_since_yield >= 16 * 1024) {
      bytes_since_yield = 0;
      lunaredu::lunaros::ota_writer_cooperative_yield();
    }
  }

  if (stream.validate != nullptr && !stream.validate(stream.context)) {
    ota_service_abort_upload();
    ota_service_report_failure("ota_stream_validation_failed");
    return false;
  }

  if (!ota_service_complete_upload()) {
    const OtaServiceStatus failed = ota_service_status_snapshot();
    const std::string complete_error = failed.message.empty()
                                           ? "ota_complete_failed"
                                           : failed.message;
    ota_service_abort_upload();
    ota_service_report_failure(complete_error.c_str());
    return false;
  }

  return true;
}

} // namespace lunaredu::lunaros
