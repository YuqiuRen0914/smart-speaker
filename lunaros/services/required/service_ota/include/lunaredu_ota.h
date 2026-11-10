#pragma once
#include "lunaros_update_service_id.h"

#include <stddef.h>
#include <stdint.h>
#include <string>
#include "lunaredu_ota_cloud.h"


namespace lunaredu::lunaros {
class DeviceCapabilityRegistry;
struct DeviceCapabilityDescriptor;

struct ServiceNode;
ServiceNode update_service_node(DeviceCapabilityRegistry &capabilities);

// A deployment policy never changes the device's partition table.
struct OtaDeploymentSnapshot {
  // Static identifiers have process lifetime; snapshots do not allocate them.
  const char *mode = ""; // dual_slot, recovery, or legacy
  const char *layout_id = "";
  const char *firmware_role = "";
  std::string error;
  bool ready = false;
  bool recovery_required = false;
  // Retention guarantee, never inferred from layout readiness or slot count.
  bool previous_business_retained = false;
  size_t business_slot_bytes = 0;
  size_t filesystem_bytes = 0;
};

// Products call this before starting peripherals/outputs. Invalid explicit
// layouts fail closed; a pending recovery handoff may restart into factory.
bool ota_service_initialize_deployment(std::string &error);
// Standalone firmware without Device/ServiceHost prepares deployment and the
// running-image confirmation window once, before admitting its own outputs.
bool ota_service_prepare_boot(std::string &error);
// Runtime may register rescue selection before loading ordinary configuration.
// Idempotent; does not start a task or admit product outputs.
void ota_service_start_boot_guard();
struct OtaRecoveryTargetSnapshot {
  bool available = false;
  bool verified = false;
  bool protected_target = false;
  uint32_t address = 0;
  uint32_t size = 0;
  std::string label;
  std::string project_name;
  std::string app_elf_sha256;
  std::string error;
};
// Read-only legacy Bootstrap candidate; successful verification is cached for
// status only. Handoff and writer admission independently verify actual flash.
OtaRecoveryTargetSnapshot ota_service_recovery_target_snapshot();
OtaDeploymentSnapshot ota_service_deployment_snapshot();
// The product calls this only after its own boot-health checks pass.
bool ota_service_confirm_running_image();



// OTA 服务状态快照。
struct OtaServiceStatus {
  bool has_result = false;
  bool success = false;
  bool upload_active = false;
  bool pull_queued = false;
  bool pull_active = false;
  uint32_t pull_job_id = 0;
  size_t pull_bytes_downloaded = 0;
  size_t pull_total_bytes = 0;
  uint8_t pull_progress_percent = 0;
  bool restart_pending = false;
  bool rollback_enabled = false;
  bool signature_verification_enabled = false;
  bool anti_rollback_enabled = false;
  bool flash_encryption_enabled = false;
  bool pending_verify = false;
  bool boot_confirmed = false;
  uint32_t boot_confirm_timeout_ms = 0;
  uint32_t boot_confirm_remaining_ms = 0;
  std::string running_image_state;
  std::string message;
  // Bounded durable boundary/checkpoint record. Upload progress is separately
  // live; persisted_bytes is only the last committed 256 KiB checkpoint.
  uint32_t install_id = 0;
  uint32_t install_target_address = 0;
  size_t persisted_bytes = 0;
  std::string install_phase;
  std::string install_sha256;
  std::string install_elf_sha256;
  std::string install_error;
  std::string journal_error;
  int reset_reason = 0;
};

// Release metadata received from the official platform. Keeping this in the
// OTA service makes the device screen and remote API consume one authority.
struct OtaAvailableUpdate {
  bool available = false;
  bool mandatory = false;
  std::string version;
  std::string summary;
  std::string changelog;
  std::string download_url;
  std::string package_sha256;
  size_t package_size = 0;
  bool role_aware = false;
  OtaCloudInstallRequest role_request;
  std::string cloud_assignment_id, cloud_release_id, cloud_assignment_source, cloud_recovery_session;
};

// OTA 固件输入流。HTTP upload、LittleFS 文件、TF 卡文件都可以适配成这个 reader。
struct OtaFirmwareStream {
  size_t content_length = 0;
  void *context = nullptr;
  int (*read_chunk)(uint8_t *buffer, size_t max_size, void *context) = nullptr;
  const char *(*read_error)(void *context) = nullptr;
  bool (*validate)(void *context) = nullptr;
  // Legacy transport authorization metadata, never an image/version bypass.
  // Project, version, partition, chip, signature and eFuse checks still apply.
  bool maintenance_authorized = false;
  // Optional transport-verified package identity for durable diagnostics.
  // Source validate() must verify it; this field itself grants no authority.
  const char *expected_sha256 = nullptr;
  // Explicit, transport-authorized Boot maintenance. This is an installation
  // intent, never an authorization bypass. The prefix must identify Bootstrap.
  bool bootstrap_update = false;
  const OtaCloudInstallRequest *role_request = nullptr;
};

enum class OtaServiceLifecycleEvent : uint8_t {
  UploadStarting,
  UploadCompleted,
  UploadAborted,
  RestartRequested,
};

using OtaServiceLifecycleCallback =
    void (*)(OtaServiceLifecycleEvent event, void *context);

// Register one product-level safety observer. The callback must not block.
void ota_service_set_lifecycle_callback(OtaServiceLifecycleCallback callback,
                                        void *context = nullptr);

// Called without the OTA status lock. entering=true synchronously confirms
// business quiescence within a product deadline; false prevents any erase.
// entering=false restores gates after failure. No handler means fail closed.
using OtaServiceMaintenanceCallback =
    bool (*)(bool entering, std::string &error, void *context);
void ota_service_set_maintenance_callback(OtaServiceMaintenanceCallback callback,
                                          void *context = nullptr);

// 获取当前 OTA 服务状态快照，适合跨任务读取。
OtaServiceStatus ota_service_status_snapshot();
struct OtaProgressSnapshot {
  bool upload_active=false,pull_active=false,restart_pending=false;
  size_t pull_total_bytes=0;
  uint8_t pull_progress_percent=0;
  bool pull_queued=false;
};
OtaProgressSnapshot ota_service_progress_snapshot();
// Allocation-free predicate for product boot-health loops.
bool ota_service_boot_pending();
// Reads SDK state directly, before Runtime/configuration initialization.
bool ota_service_image_requires_confirmation();
OtaAvailableUpdate ota_service_available_update_snapshot();

// Queue the cached official release for HTTPS download and installation.
bool ota_service_apply_available_update(std::string &error);
// Exact cached-release identity, checked against the same immutable snapshot
// used for installation. For an authorized asynchronous transport intent.
bool ota_service_apply_available_update(const char *expected_sha256,size_t expected_size,std::string &error);

// Queue a verified HTTPS pull on the shared Network job manager. Completion
// is observed through OtaServiceStatus; this call never creates a task.
bool ota_service_queue_install_from_https(const std::string &url,
                                          const std::string &expected_sha256,
                                          size_t expected_size,
                                          uint32_t &job_id,
                                          std::string &error);

// 查询 OTA 是否正在写入固件。
bool ota_service_upload_active();

// 查询 OTA 是否正在等待重启。
bool ota_service_restart_pending();

// 显式请求系统重启；用于已提交 OTA 后的补充触发或远程维护重启。
void ota_service_request_restart();
bool ota_service_try_request_restart(std::string &error);
// Transport must authorize this command; the registered product observer stops
// outputs before durable handoff to v3 factory or verified legacy Bootstrap.
bool ota_service_request_recovery(std::string &error);
// 拒绝当前待确认镜像并立即回滚重启。成功时该函数不会返回。
// 可在 OTA 服务初始化之前调用；非待确认镜像保持不变。
bool ota_service_rollback_running_image(const char *reason);

// Record a source-side failure before an OTA upload session starts.
void ota_service_report_failure(const char *message);

// 从任意固件输入流安装 OTA。具体文件来源由调用方适配成 OtaFirmwareStream。
bool ota_service_install_stream(const OtaFirmwareStream &stream);
bool ota_service_install_stream(const OtaFirmwareStream &stream, std::string &error);

} // namespace lunaredu::lunaros
