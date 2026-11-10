#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace lunaredu::lunaros {
inline constexpr bool ota_cloud_recovery_layout(std::string_view layout) { return layout=="lunaros-16m-v3-recovery" || layout=="lunaros-16m-v4-field"; }
struct OtaCloudImage {
  bool present = false, confirmed = false;
  std::string project_name, version, app_elf_sha256, sha256;
};
struct OtaCloudInventory {
  bool ready = false;
  std::string layout_id, hardware_id, running_role;
  OtaCloudImage boot, application;
};
struct OtaCloudInstallRequest {
  uint32_t protocol_version = 2;
  std::string url, sha256, app_elf_sha256, project_name, version;
  std::string target_role, layout_id, hardware_id;
  size_t size = 0;
};
// OTA owns SDK/partition reads and caches complete image verification. A BIN
// digest is returned only from a matching durable, validated transport journal.
OtaCloudInventory ota_service_cloud_inventory();
bool ota_service_queue_role_install_from_https(const OtaCloudInstallRequest &,
    uint32_t &job_id, std::string &error);
// Pure owner checks shared by admission, prefix validation and host regression.
bool ota_cloud_request_allowed(const OtaCloudInstallRequest &, const OtaCloudInventory &,
    std::string &error);
bool ota_cloud_prefix_allowed(const OtaCloudInstallRequest &, const uint8_t *, size_t,
    std::string &error);
}
