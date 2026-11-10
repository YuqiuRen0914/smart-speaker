#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

#include "lunaredu_lunaros/model/device_model.h"

namespace lunaredu::lunaros {

struct ServiceNode;
ServiceNode update_service_node();

const DeviceCapabilityDescriptor &ota_capability_descriptor();

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
};

struct OtaPartitionSnapshot {
  std::string label;
  uint32_t address = 0;
  uint32_t size = 0;
};

struct OtaImageMetadataSnapshot {
  std::string project_name;
  std::string version;
  std::string build_date;
  std::string build_time;
  OtaPartitionSnapshot running;
  OtaPartitionSnapshot boot;
  OtaPartitionSnapshot next;
};

// OTA 固件输入流。HTTP upload、LittleFS 文件、TF 卡文件都可以适配成这个 reader。
struct OtaFirmwareStream {
  size_t content_length = 0;
  void *context = nullptr;
  int (*read_chunk)(uint8_t *buffer, size_t max_size, void *context) = nullptr;
  const char *(*read_error)(void *context) = nullptr;
  bool (*validate)(void *context) = nullptr;
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
void ota_service_clear_lifecycle_callback();

// 初始化 OTA 系统服务并注册对应 capability。
void setup_ota_service();

// 推进 OTA 服务的延迟重启逻辑。
void tick_ota_service();

// 获取当前 OTA 服务状态快照，适合跨任务读取。
OtaServiceStatus ota_service_status_snapshot();
// Allocation-free predicate for product boot-health loops.
bool ota_service_boot_pending();
OtaAvailableUpdate ota_service_available_update_snapshot();
OtaImageMetadataSnapshot ota_service_image_metadata_snapshot();

// Replace the cached release metadata after a successful official-platform
// presence/check response. An unavailable update clears stale release data.
void ota_service_set_available_update(const OtaAvailableUpdate &update);

// Queue the cached official release for HTTPS download and installation.
bool ota_service_apply_available_update(std::string &error);

// Mark the short asynchronous ticket-refresh phase before a device-screen
// update starts. A following pull queue or failure replaces this state.
void ota_service_report_preparing(const char *message);

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

// 确认当前 OTA 镜像已通过产品级启动自检，并取消 ESP-IDF 回滚。
// 非待确认镜像调用该函数是幂等的。
bool ota_service_confirm_running_image();

// 拒绝当前待确认镜像并立即回滚重启。成功时该函数不会返回。
// 可在 OTA 服务初始化之前调用；非待确认镜像保持不变。
bool ota_service_rollback_running_image(const char *reason);

// 开始一次新的 OTA 上传。
// 返回 true 表示 OTA 会话已打开；失败原因写入 OTA 状态。
bool ota_service_begin_upload(size_t image_size);

// 写入一段 OTA 固件数据。
bool ota_service_write_upload_chunk(uint8_t *data, size_t size);

// 结束 OTA 上传并提交固件。
bool ota_service_complete_upload();

// 中止当前 OTA 上传。
void ota_service_abort_upload();

// Record a source-side failure before an OTA upload session starts.
void ota_service_report_failure(const char *message);

// 从任意固件输入流安装 OTA。具体文件来源由调用方适配成 OtaFirmwareStream。
bool ota_service_install_stream(const OtaFirmwareStream &stream);

} // namespace lunaredu::lunaros
