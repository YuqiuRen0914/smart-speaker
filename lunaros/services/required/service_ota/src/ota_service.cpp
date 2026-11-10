#include "lunaredu_ota.h"
#include "espnow_ota.h"
#include "kernel/platform_operation.h"
#include <esp_log.h>

#include <sdkconfig.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cstdio>
#if defined(ESP_PLATFORM) || defined(LUNAROS_OTA_TEST_SETTINGS)
#include "configuration/settings.h"
#endif
#ifdef ESP_PLATFORM
#include "ota_install_intent.h"
#include <esp_system.h>
#include "system/boot_guard.h"
#endif
#include <mutex>

#include "model/device_model.h"
#include "platform/system.h"
#include "lunaros_ota_lifecycle.h"
#include "ota_version.h"
#include "ota_writer.h"
#include "lunaros_network_jobs.h"
#if defined(ESP_PLATFORM) || CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT || CONFIG_LUNAREDU_OTA_DUAL_SLOT_LAYOUT
#include "recovery_internal.h"
#include "ota_deployment.h"
#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#include <cstring>
#endif

#ifndef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#define CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 0
#endif

#ifndef CONFIG_LUNAREDU_SYSTEM_OTA_BOOT_CONFIRM_TIMEOUT_MS
#define CONFIG_LUNAREDU_SYSTEM_OTA_BOOT_CONFIRM_TIMEOUT_MS 120000
#endif

namespace lunaredu::lunaros {

namespace {

const DeviceCapabilityDescriptor kOtaCapability = []() {
  DeviceCapabilityDescriptor descriptor;
  descriptor.id = "ota_service";
  return descriptor;
}();

OtaServiceStatus g_ota_status;
struct InstallRecord {
  uint32_t magic = 0x3141544f;
  uint32_t schema = 1;
  uint32_t id = 0;
  uint32_t written = 0;
  uint32_t target_address = 0;
  char phase[24]{};
  char sha256[65]{};
  char elf_sha256[65]{};
  char error[120]{};
  uint32_t crc = 0;
};
InstallRecord g_install_record;
uint32_t journal_checksum(const InstallRecord &record) {
  const auto *bytes = reinterpret_cast<const uint8_t *>(&record);
  uint32_t hash = 2166136261U;
  for (size_t i = 0; i < offsetof(InstallRecord, crc); ++i)
    hash = (hash ^ bytes[i]) * 16777619U;
  return hash;
}
OtaAvailableUpdate g_available_update;
bool g_ota_upload_active = false;
PlatformOperation g_update_operation;
PlatformOperation g_restart_operation;
std::recursive_mutex g_ota_mutex;
int64_t g_boot_confirm_deadline_ms = 0;
bool g_recovery_confirmation_pending = false;
int64_t g_recovery_confirmation_retry_ms = 0;
OtaServiceLifecycleCallback g_lifecycle_callback = nullptr;
void *g_lifecycle_context = nullptr;
OtaServiceMaintenanceCallback g_maintenance_callback = nullptr;
void *g_maintenance_context = nullptr;
bool g_maintenance_prepared = false;
bool g_pull_reserved = false;
// Covers prefix/source reads and validation as well as SDK handle ownership.
std::atomic<bool> g_stream_busy{false};
class StreamAdmission {
 public:
  StreamAdmission() {
    bool expected = false;
    acquired_ = g_stream_busy.compare_exchange_strong(expected, true);
  }
  ~StreamAdmission() { if (acquired_) g_stream_busy.store(false); }
  explicit operator bool() const { return acquired_; }
 private:
  bool acquired_ = false;
};

void project_install_record_locked() {
  g_ota_status.install_id = g_install_record.id;
  g_ota_status.install_target_address = g_install_record.target_address;
  g_ota_status.persisted_bytes = g_install_record.written;
  g_ota_status.install_phase = g_install_record.phase;
  g_ota_status.install_sha256 = g_install_record.sha256;
  g_ota_status.install_elf_sha256 = g_install_record.elf_sha256;
  g_ota_status.install_error = g_install_record.error;
}
void journal_phase(const char *phase, size_t bytes, const char *error = "",
                   const char *sha256 = nullptr, bool begin = false) {
  InstallRecord record;
  {
    std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
    if (begin) {
      const uint32_t next = g_install_record.id + 1;
      g_install_record = {};
      g_install_record.id = next ? next : 1;
    }
    g_install_record.written = static_cast<uint32_t>(bytes);
    std::snprintf(g_install_record.phase, sizeof(g_install_record.phase), "%s", phase);
    std::snprintf(g_install_record.error, sizeof(g_install_record.error), "%s", error);
    if (sha256) std::snprintf(g_install_record.sha256, sizeof(g_install_record.sha256), "%s", sha256);
    g_install_record.crc = journal_checksum(g_install_record);
    record = g_install_record;
    project_install_record_locked();
  }
#if defined(ESP_PLATFORM) || defined(LUNAROS_OTA_TEST_SETTINGS)
  std::string storage_error;
  const std::string payload(reinterpret_cast<const char *>(&record), sizeof(record));
  // This diagnostic record must not block independent recovery when ordinary
  // NVS is damaged; a separate error exposes loss of durability.
  (void)settings_write_blob("ota_state", "journal", payload, storage_error);
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  g_ota_status.journal_error = storage_error;
#else
  (void)record;
#endif
}
void load_journal_locked() {
  g_install_record = {};
#ifdef ESP_PLATFORM
  g_ota_status.reset_reason = static_cast<int>(esp_reset_reason());
#endif
#if defined(ESP_PLATFORM) || defined(LUNAROS_OTA_TEST_SETTINGS)
  std::string value, error;
  const auto found = settings_read_blob("ota_state", "journal", sizeof(InstallRecord), value, error);
  if (found == SettingsReadResult::Found && value.size() == sizeof(InstallRecord)) {
    InstallRecord candidate;
    std::memcpy(&candidate, value.data(), sizeof(candidate));
    if (candidate.magic == 0x3141544f && candidate.schema == 1 &&
        candidate.crc == journal_checksum(candidate) &&
        candidate.phase[sizeof(candidate.phase)-1] == 0 &&
        candidate.sha256[sizeof(candidate.sha256)-1] == 0 &&
        candidate.elf_sha256[sizeof(candidate.elf_sha256)-1] == 0 &&
        candidate.error[sizeof(candidate.error)-1] == 0) {
      g_install_record = candidate;
    } else error = "ota_journal_corrupt";
  } else if (found == SettingsReadResult::Found) error = "ota_journal_size_invalid";
  g_ota_status.journal_error = error;
#endif
  project_install_record_locked();
  if (g_ota_status.install_phase == "preparing" || g_ota_status.install_phase == "writing")
    g_ota_status.install_phase = "interrupted";
}

int64_t now_ms() {
  return lunaredu::lunaros::system_monotonic_ms();
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
  g_recovery_confirmation_pending = false;
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  const auto recovery = ota_recovery_snapshot();
  g_recovery_confirmation_pending = recovery.ready && recovery.phase == "boot_pending";
#endif
  g_ota_status.boot_confirmed = !image_status.pending_verify && !g_recovery_confirmation_pending;
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

void clear_ota_session_locked(bool release_admission = true) {
  if (release_admission) g_update_operation.end();
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
  std::unique_lock<std::recursive_mutex> lock(g_ota_mutex);
  g_ota_status = OtaServiceStatus();
  load_journal_locked();
  g_available_update = {};
  g_pull_reserved = false;
  g_maintenance_prepared = false;
  clear_ota_session_locked();
  inspect_running_image_locked();
  if ((g_ota_status.install_phase == "boot_selected" || g_ota_status.install_phase == "verified") &&
      g_ota_status.boot_confirmed) {
    const auto running = ota_writer_metadata();
    if (!g_ota_status.install_elf_sha256.empty() &&
        running.app_elf_sha256 == g_ota_status.install_elf_sha256 &&
        running.running.address == g_ota_status.install_target_address)
      journal_phase("confirmed", g_ota_status.persisted_bytes);
    else journal_phase("boot_mismatch", g_ota_status.persisted_bytes, "target_image_not_running");
  }
  lock.unlock();
  ota_service_start_boot_guard();
}

void ota_service_start_boot_guard() {
#ifdef ESP_PLATFORM
  std::string error;
  (void)system_boot_guard_start([](std::string &failure, void *) {
    return ota_writer_select_rescue(failure);
  }, nullptr, error);
#endif
}

bool ota_service_prepare_boot(std::string &error) {
  ota_service_start_boot_guard();
  if (!ota_service_initialize_deployment(error)) return false;
  setup_ota_service();
  error.clear();
  return true;
}

void ota_service_set_lifecycle_callback(OtaServiceLifecycleCallback callback,
                                        void *context) {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  g_lifecycle_callback = callback;
  g_lifecycle_context = context;
}

void ota_service_set_maintenance_callback(OtaServiceMaintenanceCallback callback,
                                          void *context) {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  g_maintenance_callback = callback;
  g_maintenance_context = context;
}
bool ota_service_prepare_maintenance(std::string &error) {
  OtaServiceMaintenanceCallback callback;
  void *context;
  {
    std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
    if (g_ota_status.pending_verify || !g_ota_status.boot_confirmed) {
      error = "ota_boot_not_confirmed"; return false;
    }
    if (g_maintenance_prepared) { error.clear(); return true; }
    callback = g_maintenance_callback;
    context = g_maintenance_context;
  }
  if (!callback) { error = "ota_maintenance_handler_unavailable"; return false; }
  if (!begin_network_ota_maintenance(error)) return false;
  if (!callback(true, error, context)) {
    std::string ignored;
    (void)callback(false, ignored, context);
    end_network_ota_maintenance();
    if (error.empty()) error = "ota_maintenance_not_ready";
    return false;
  }
  // Application stop can acknowledge just before its I/O lane releases the
  // physical-work permit. Wait on that accounting, never erase flash early.
  const auto activation_started = system_monotonic_ms();
  bool activated = false;
  do {
    activated = activate_network_ota_maintenance(error);
    if (activated || error != "network_external_work_retirement_pending" ||
        uint32_t(system_monotonic_ms() - activation_started) >= 5000) break;
    system_delay_ms(1);
  } while (true);
  if (!activated) {
    std::string ignored;
    (void)callback(false, ignored, context);
    end_network_ota_maintenance();
    return false;
  }
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  g_maintenance_prepared = true;
  error.clear(); return true;
}
void ota_service_release_maintenance() {
  OtaServiceMaintenanceCallback callback;
  void *context;
  {
    std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
    if (!g_maintenance_prepared) return;
    g_maintenance_prepared = false;
    callback = g_maintenance_callback;
    context = g_maintenance_context;
  }
  std::string ignored;
  end_network_ota_maintenance();
  if (callback) (void)callback(false, ignored, context);
}
bool ota_service_reserve_pull(std::string &error) {
  StreamAdmission session;
  if (!static_cast<bool>(session)) { error = "ota_upload_in_progress"; return false; }
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  if (g_pull_reserved || g_ota_upload_active || g_ota_status.restart_pending) {
    error = "ota_busy"; return false;
  }
  if (g_ota_status.pending_verify || !g_ota_status.boot_confirmed) {
    error = "ota_boot_not_confirmed"; return false;
  }
  g_pull_reserved = true;
  g_ota_status.pull_queued = true;
  g_ota_status.pull_job_id = 0;
  g_ota_status.message = "ota_pull_preparing";
  error.clear(); return true;
}
void ota_service_release_pull() {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  g_pull_reserved = false;
  g_ota_status.pull_queued = false;
  g_ota_status.pull_active = false;
}

void tick_ota_service() {
  internal::poll_espnow_ota();
#ifdef ESP_PLATFORM
  system_boot_guard_heartbeat(BootGuardOwner::Main);
  ota_service_tick_recovery_install();
#endif
  bool restart_pending = false;
  bool rollback_due = false;
  bool reconcile_confirmation = false;
  {
    std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
    refresh_boot_confirmation_remaining_locked();
    rollback_due = g_ota_status.pending_verify && g_boot_confirm_deadline_ms > 0 &&
                   now_ms() >= g_boot_confirm_deadline_ms;
    if (rollback_due) g_ota_status.message = "boot_confirmation_timeout";
    restart_pending = g_ota_status.restart_pending;
    if (g_recovery_confirmation_pending && !g_ota_status.pending_verify &&
        now_ms() >= g_recovery_confirmation_retry_ms) {
      reconcile_confirmation = true;
      g_recovery_confirmation_retry_ms = now_ms() + 5000;
    }
  }
  if (reconcile_confirmation) (void)ota_service_confirm_running_image();
  if (rollback_due) {
    (void)ota_service_rollback_running_image("boot_confirmation_timeout");
    return;
  }
  if (restart_pending) system_restart_after_delay(200);
}

OtaProgressSnapshot ota_service_progress_snapshot() {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  return {g_ota_status.upload_active||g_stream_busy.load(),g_ota_status.pull_active,
          g_ota_status.restart_pending,g_ota_status.pull_total_bytes,g_ota_status.pull_progress_percent,g_ota_status.pull_queued};
}

OtaServiceStatus ota_service_status_snapshot() {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  refresh_boot_confirmation_remaining_locked();
  auto snapshot = g_ota_status;
  snapshot.upload_active = snapshot.upload_active || g_stream_busy.load();
  return snapshot;
}

bool ota_service_image_requires_confirmation() {
  return ota_writer_image_status().pending_verify;
}

bool ota_service_boot_pending() {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  return g_ota_status.pending_verify;
}

bool ota_service_upload_active() {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  return g_ota_upload_active || g_stream_busy.load();
}

bool ota_service_restart_pending() {
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  return g_ota_status.restart_pending;
}

bool ota_service_try_request_restart(std::string &error) {
  error.clear();
  StreamAdmission session;
  if (!session) { error = "ota_upload_in_progress"; return false; }
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  if (g_pull_reserved || g_ota_upload_active) { error = "ota_busy"; return false; }
  if (g_ota_status.restart_pending) return true;
  if (!g_restart_operation.begin(execution_effect(ExecutionEffect::Reset))) {
    g_ota_status.has_result = true;
    g_ota_status.success = false;
    g_ota_status.message = error = "restart_execution_not_admitted";
    return false;
  }
  notify_lifecycle_locked(OtaServiceLifecycleEvent::RestartRequested);
  g_ota_status.has_result = true;
  g_ota_status.success = true;
  g_ota_status.restart_pending = true;
  if (g_ota_status.message.empty() || g_ota_status.message == "ota_restart_pending")
    g_ota_status.message = "restart_requested";
  return true;
}
void ota_service_request_restart() {
  std::string ignored;
  (void)ota_service_try_request_restart(ignored);
}

bool ota_service_request_recovery(std::string &error) {
  StreamAdmission session;
  if (!session) { error = "ota_busy"; return false; }
  {
    std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
    if (g_pull_reserved || g_ota_upload_active || g_ota_status.pull_queued ||
        g_ota_status.pull_active || g_ota_status.restart_pending) {
      error = "ota_busy"; return false;
    }
  }
  // This is an explicit handoff to Boot, not the crash-rescue callback.
  // Re-entering Boot must never invalidate it or erase the retained app selector.
  if (ota_writer_metadata().project_name == "lunaros-bootstrap") {
    error = "ota_already_recovery"; return false;
  }
  if (!ota_service_prepare_maintenance(error)) return false;
  if (!g_restart_operation.begin(execution_effect(ExecutionEffect::Reset) |
      execution_effect(ExecutionEffect::FlashWrite))) {
    error = "recovery_execution_not_admitted";
    ota_service_release_maintenance(); return false;
  }
  {
    std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
    notify_lifecycle_locked(OtaServiceLifecycleEvent::RestartRequested);
  }
  const bool selected =
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
      ota_recovery_request(error);
#else
      ota_writer_select_rescue(error);
#endif
  if (!selected) {
    g_restart_operation.end(); ota_service_release_maintenance(); return false;
  }
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  g_ota_status.has_result = true;
  g_ota_status.success = true;
  g_ota_status.restart_pending = true;
  g_ota_status.message = "recovery_restart_pending";
  return true;
}

bool ota_service_repair_recovery_state(std::string &error) {
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  StreamAdmission session;
  if (!static_cast<bool>(session)) { error = "ota_busy"; return false; }
  {
    std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
    if (g_pull_reserved || g_ota_upload_active || g_ota_status.restart_pending) {
      error = "ota_busy"; return false;
    }
  }
  PlatformOperation repair;
  if (!repair.begin(execution_effect(ExecutionEffect::FlashWrite))) {
    error = "recovery_execution_not_admitted"; return false;
  }
  if (!ota_recovery_repair_state(error)) return false;
#ifdef ESP_PLATFORM
  return ota_intent_clear(error);
#else
  return true;
#endif
#else
  error = "recovery_layout_disabled"; return false;
#endif
}

bool ota_service_confirm_running_image() {
  PlatformOperation confirmation;
  bool pending_verify = false;
  {
    std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
    pending_verify = g_ota_status.pending_verify || g_recovery_confirmation_pending;
    if (!pending_verify) {
      g_ota_status.boot_confirmed = true;
      g_ota_status.boot_confirm_remaining_ms = 0;
      g_boot_confirm_deadline_ms = 0;
      return true;
    }
    if (!confirmation.begin(execution_effect(ExecutionEffect::FlashWrite))) {
      g_ota_status.message = "boot_confirm_execution_not_admitted";
      return false;
    }
  }

  std::string writer_error;
  const bool confirmed =
      lunaredu::lunaros::ota_writer_confirm_image(writer_error);
  std::unique_lock<std::recursive_mutex> lock(g_ota_mutex);
  if (!confirmed) {
    g_ota_status.message = "boot_confirm_failed:" + writer_error;
    return false;
  }
  g_ota_status.pending_verify = false;
  g_recovery_confirmation_pending = false;
  g_ota_status.boot_confirmed = true;
  g_ota_status.boot_confirm_remaining_ms = 0;
  g_ota_status.running_image_state = "valid";
  g_ota_status.message = "boot_confirmed";
  g_boot_confirm_deadline_ms = 0;
  ESP_LOGI("system.ota", "running OTA image confirmed");
  const auto bytes = g_ota_status.persisted_bytes;
  const auto target_elf = g_ota_status.install_elf_sha256;
  const auto target_address = g_ota_status.install_target_address;
  lock.unlock();
  const auto running = ota_writer_metadata();
  if (!target_elf.empty() && running.app_elf_sha256 == target_elf &&
      running.running.address == target_address)
    journal_phase("confirmed", bytes);
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
  return ota_service_apply_available_update(nullptr,0,error);
}
bool ota_service_apply_available_update(const char *expected_sha256,size_t expected_size,std::string &error) {
  OtaAvailableUpdate update;
  {
    std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
    update = g_available_update;
  }
  if(expected_sha256&&(update.package_sha256!=expected_sha256||update.package_size!=expected_size)){
    error="ota_available_release_changed";return false;
  }
  if (!update.available || update.download_url.empty() ||
      update.package_sha256.empty() || update.package_size == 0) {
    error = "ota_update_unavailable";
    ota_service_report_failure(error.c_str());
    return false;
  }
  uint32_t job_id = 0;
  const bool queued = update.role_aware ? ota_service_queue_role_install_from_https(update.role_request,job_id,error)
      : ota_service_queue_install_from_https(update.download_url,update.package_sha256,update.package_size,job_id,error);
  if (!queued) {
    ota_service_report_failure(error.c_str());
  }
  return queued;
}

bool ota_service_rollback_running_image(const char *reason) {
  PlatformOperation rollback;
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
    if (!rollback.begin(execution_effect(ExecutionEffect::FlashWrite) |
        execution_effect(ExecutionEffect::Reset))) {
      g_ota_status.message = "rollback_execution_not_admitted";
      return false;
    }
    g_ota_status.message =
        reason != nullptr && reason[0] != '\0' ? reason : "rollback_requested";
    g_ota_status.boot_confirm_remaining_ms = 0;
    g_boot_confirm_deadline_ms = 0;
  }

  ESP_LOGE("system.ota", "rejecting running OTA image reason=%s",
       reason != nullptr ? reason : "rollback_requested");
  std::string writer_error;
  (void)lunaredu::lunaros::ota_writer_rollback_and_reboot(writer_error);
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  g_ota_status.message = "rollback_failed:" + writer_error;
  return false;
}

static bool ota_service_begin_upload(size_t image_size, bool maintenance_authorized, bool pull_session,
                                     bool bootstrap_update) {
  {
    std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
    if (g_ota_upload_active || g_ota_status.restart_pending) return false;
  }
  std::string writer_error;
  if (!ota_service_prepare_maintenance(writer_error)) {
    ota_service_report_failure(writer_error.c_str()); return false;
  }
#if defined(ESP_PLATFORM) && CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  if (!pull_session && !ota_intent_clear(writer_error)) {
    ota_service_report_failure(writer_error.c_str()); return false;
  }
#else
  (void)pull_session;
#endif
  {
    std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
    if (!g_update_operation.begin(execution_effect(ExecutionEffect::FlashWrite) |
        execution_effect(ExecutionEffect::Reset))) {
      g_ota_status.has_result = true;
      g_ota_status.success = false;
      g_ota_status.message = "ota_execution_not_admitted";
      return false;
    }
    notify_lifecycle_locked(OtaServiceLifecycleEvent::UploadStarting);
    g_ota_status.success = false;
    g_ota_status.has_result = false;
    g_ota_status.message = "upload_started";
    g_ota_status.pull_bytes_downloaded = 0;
    g_ota_status.pull_total_bytes = image_size;
    g_ota_status.pull_progress_percent = 0;
    set_ota_upload_active_locked(true);
  }
  const bool began = ota_writer_begin(image_size, writer_error, maintenance_authorized,
      [](const std::string &elf, size_t bytes, uint32_t target_address) {
        {
          std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
          std::snprintf(g_install_record.elf_sha256, sizeof(g_install_record.elf_sha256), "%s", elf.c_str());
          g_install_record.target_address = target_address;
        }
        // Durable validated identity precedes otadata mutation. A reset at the
        // following boot-select/ACK boundaries is reconciled by ELF AND slot.
        journal_phase("verified", bytes);
      }, bootstrap_update);
  if (began) { journal_phase("writing", 0); return true; }
  journal_phase("failed", 0, writer_error.c_str());
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
  clear_ota_session_locked();
  g_ota_status.has_result = true;
  g_ota_status.message = writer_error;
  notify_lifecycle_locked(OtaServiceLifecycleEvent::UploadAborted);
  return false;
}

static bool ota_service_write_upload_chunk(uint8_t *data, size_t size) {
  std::unique_lock<std::recursive_mutex> lock(g_ota_mutex);
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
  lock.unlock();
  const bool written = ota_writer_write(data, size, writer_error);
  lock.lock();
  if (written) {
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

static bool ota_service_complete_upload() {
  std::unique_lock<std::recursive_mutex> lock(g_ota_mutex);
  if (!g_ota_upload_active) {
    g_ota_status.success = false;
    g_ota_status.has_result = true;
    g_ota_status.message = "ota_upload_not_started";
    return false;
  }

  std::string writer_error;
  lock.unlock();
  const bool completed = ota_writer_complete(writer_error);
  lock.lock();
  clear_ota_session_locked(!completed || !ota_writer_last_candidate_selected());

  if (completed) {
    g_ota_status.success = true;
    g_ota_status.has_result = true;
    g_ota_status.message = "ota_success_verified";
    return true;
  }

  g_ota_status.success = false;
  g_ota_status.has_result = true;
  g_ota_status.message = writer_error;
  return false;
}

static void ota_service_abort_upload() {
  ota_writer_abort();
  std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
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
  std::unique_lock<std::recursive_mutex> lock(g_ota_mutex);
  if (g_ota_upload_active || g_ota_status.restart_pending) return;
  // A source failure is not the end of a reserved pull job. Keep its busy
  // boundary until the runner atomically queues its retry or finishes it.
  if (!g_pull_reserved) {
    g_ota_status.pull_queued = false;
    g_ota_status.pull_active = false;
  }
  g_ota_status.success = false;
  g_ota_status.has_result = true;
  g_ota_status.restart_pending = false;
  g_ota_status.message = message != nullptr && message[0] != '\0'
                             ? message
                             : "ota_source_failed";
  const auto failure = g_ota_status.message;
  const auto bytes = g_ota_status.pull_bytes_downloaded;
  const bool transaction = g_install_record.id != 0 &&
      (g_ota_status.install_phase == "preparing" || g_ota_status.install_phase == "writing" ||
       g_ota_status.install_phase == "verified");
  lock.unlock();
  if (transaction) journal_phase("failed", bytes, failure.c_str());
}

static bool install_stream_owned(const OtaFirmwareStream &stream, uint32_t pull_job_id,
                                 std::string *rejection = nullptr) {
  StreamAdmission session;
  if (!static_cast<bool>(session)) {
    if (rejection) *rejection = "ota_upload_in_progress";
    return false;
  }
  {
    std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
    if (g_ota_upload_active || g_ota_status.restart_pending ||
        (g_pull_reserved && (pull_job_id == 0 ||
          pull_job_id != g_ota_status.pull_job_id || !g_ota_status.pull_active))) {
      if (rejection) *rejection = g_ota_status.restart_pending ? "ota_restart_pending" : "ota_busy";
      return false;
    }
    if (pull_job_id != 0 && !g_pull_reserved) {
      if (rejection) *rejection = "ota_pull_context_stale";
      return false;
    }
  }
  struct MaintenanceCleanup {
    bool keep = false;
    ~MaintenanceCleanup() { if (!keep) ota_service_release_maintenance(); }
  } maintenance{pull_job_id != 0};
  if (stream.content_length == 0 || stream.read_chunk == nullptr) {
    std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
    g_ota_status.success = false;
    g_ota_status.has_result = true;
    g_ota_status.restart_pending = false;
    g_ota_status.message = "empty_ota_stream";
    return false;
  }

  if (stream.expected_sha256 != nullptr &&
      (std::strlen(stream.expected_sha256) != 64 || stream.validate == nullptr)) {
    ota_service_report_failure("ota_digest_validator_missing"); return false;
  }
  const OtaImageMetadataSnapshot image = ota_writer_metadata();
#if CONFIG_LUNAREDU_OTA_DUAL_SLOT_LAYOUT
  std::string layout_error;
  if (!ota_service_initialize_deployment(layout_error)) {
    ota_service_report_failure(layout_error.c_str()); return false;
  }
#endif
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  if (!stream.bootstrap_update && !ota_recovery_snapshot().recovery_running) {
    ota_service_report_failure("ota_recovery_required");
    return false;
  }
#endif
  const bool running_boot = image.project_name == "lunaros-bootstrap";
  if (stream.bootstrap_update == running_boot) {
    ota_service_report_failure(running_boot ? "ota_boot_requires_application" : "ota_recovery_required");
    return false;
  }
  size_t capacity = image.next.size;
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  if (stream.bootstrap_update) {
    const auto *factory = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_FACTORY, "recovery");
    capacity = factory ? factory->size : 0;
  }
#endif
  if (capacity == 0 || stream.content_length > capacity) {
    ota_service_report_failure("ota_image_exceeds_partition");
    return false;
  }

  uint8_t buffer[1024];
  size_t prefix_size = 0;
#if defined(ESP_PLATFORM) || CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT || CONFIG_LUNAREDU_OTA_DUAL_SLOT_LAYOUT
  constexpr size_t required = recovery::kContractOffset + sizeof(recovery::ImageContract);
  static_assert(required <= sizeof(buffer));
  if (stream.content_length < required) {
    ota_service_report_failure("ota_v3_contract_missing"); return false;
  }
  while (prefix_size < required) {
    const size_t capacity = required - prefix_size;
    const int size = stream.read_chunk(buffer + prefix_size, capacity, stream.context);
    if (size <= 0 || static_cast<size_t>(size) > capacity) {
      ota_service_report_failure("ota_preflight_read_failed"); return false;
    }
    prefix_size += static_cast<size_t>(size);
  }
  std::string preflight_error;
  if (!(stream.bootstrap_update ? ota_boot_validate_header(buffer, prefix_size, preflight_error)
                               : ota_deployment_validate_header(buffer, prefix_size, preflight_error))) {
    ota_service_report_failure(preflight_error.c_str()); return false;
  }
  if (stream.role_request &&
      !ota_cloud_prefix_allowed(*stream.role_request,buffer,prefix_size,preflight_error)) {
    ota_service_report_failure(preflight_error.c_str()); return false;
  }
  esp_image_header_t header{};
  esp_app_desc_t candidate{};
  std::memcpy(&header, buffer, sizeof(header));
  std::memcpy(&candidate, buffer + sizeof(header) + sizeof(esp_image_segment_header_t), sizeof(candidate));
  if (!stream.bootstrap_update && !std::memcmp(candidate.project_name, "lunaros-bootstrap", sizeof("lunaros-bootstrap"))) {
    ota_service_report_failure("ota_application_image_required"); return false;
  }
  const esp_err_t validity = esp_ota_check_image_validity(ESP_PARTITION_TYPE_APP, &header,
      stream.bootstrap_update ? nullptr : &candidate);
  if (candidate.magic_word != ESP_APP_DESC_MAGIC_WORD || validity != ESP_OK) {
    ota_service_report_failure("ota_v3_image_header_invalid"); return false;
  }
#endif
  journal_phase("preparing", 0, "", stream.expected_sha256, true);
  if (!ota_service_begin_upload(stream.content_length, stream.maintenance_authorized,
                                pull_job_id != 0, stream.bootstrap_update)) {
    const auto status = ota_service_status_snapshot();
    journal_phase("failed", 0, status.message.c_str());
    return false;
  }

  if (prefix_size && !ota_service_write_upload_chunk(buffer, prefix_size)) {
    const auto failed = ota_service_status_snapshot();
    ota_service_abort_upload();
    ota_service_report_failure(failed.message.c_str());
    return false;
  }
  size_t remaining = stream.content_length - prefix_size;
  size_t bytes_since_yield = 0;
  size_t next_checkpoint = 256 * 1024;
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
    const size_t written = stream.content_length - remaining;
    if (written >= next_checkpoint) {
      journal_phase("writing", written);
      next_checkpoint = written + 256 * 1024;
    }
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

  {
    const auto elf = ota_writer_last_candidate_elf_sha256();
    std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
    std::snprintf(g_install_record.elf_sha256, sizeof(g_install_record.elf_sha256), "%s", elf.c_str());
  }
  const bool selected = ota_writer_last_candidate_selected();
  journal_phase(selected ? "boot_selected" : "boot_updated", stream.content_length);
  {
    std::lock_guard<std::recursive_mutex> lock(g_ota_mutex);
    g_ota_status.restart_pending = selected;
    g_ota_status.message = selected ? "ota_success_restarting" : "ota_boot_updated";
    notify_lifecycle_locked(OtaServiceLifecycleEvent::UploadCompleted);
  }
  maintenance.keep = selected;
  return true;
}
bool ota_service_install_stream(const OtaFirmwareStream &stream) {
  return install_stream_owned(stream, 0);
}
bool ota_service_install_stream(const OtaFirmwareStream &stream, std::string &error) {
  error.clear();
  const bool success = install_stream_owned(stream, 0, &error);
  if (!success && error.empty()) error = ota_service_status_snapshot().message;
  return success;
}
bool ota_service_install_pull_stream(const OtaFirmwareStream &stream, uint32_t job_id) {
  return install_stream_owned(stream, job_id);
}

} // namespace lunaredu::lunaros
