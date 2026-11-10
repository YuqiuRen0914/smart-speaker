#include "applications_service_internal.h"
#include "app/package/package_validation.h"
#include "app/package/application_package_policy.h"
#include "app/package/package_trust.h"
#include "app/package/application_limits.h"
#include "lunaros_storage.h"
namespace lunaredu::lunaros::apps::runtime_detail {
bool owned_by_other(const void *owner, const std::string &id) {
  for (const auto &[context, application] : active_owners)
    if (context != owner && application == id) return true;
  return false;
}
bool read_verified_package(const Application &indexed, std::vector<uint8_t> &bytes,
                           Application &checked, size_t &offset, std::string &error) {
  if (!storage_read_file_bounded(application_package_path(indexed).c_str(), kMaximumPackageBytes, bytes)) {
    error="已安装应用读取失败"; return false;
  }
  return verify_package(indexed, bytes, checked, offset, error);
}
bool verify_package(const Application &indexed, PackageBytes bytes,
                    Application &checked, size_t &offset, std::string &error) {
  error.clear();
  if (!bytes.data() || bytes.size() > kMaximumPackageBytes || bytes.size() != indexed.size ||
      digest(bytes.data(), bytes.size()) != indexed.sha256 ||
      !verify_aot_package_signature(indexed, error) || !unpack(bytes, checked, offset, error) ||
      checked.id != indexed.id || checked.version != indexed.version ||
      checked.backend != indexed.backend ||
      checked.entry_format != indexed.entry_format ||
      checked.private_storage != indexed.private_storage || checked.tick_hz != indexed.tick_hz ||
      checked.device_basic != indexed.device_basic ||
      checked.name != indexed.name || checked.color != indexed.color ||
      checked.icon_resource != indexed.icon_resource ||
      checked.source_sha256 != indexed.source_sha256 ||
      checked.entry_sha256 != indexed.entry_sha256 ||
      checked.package_version != indexed.package_version ||
      checked.artifact_id != indexed.artifact_id) {
    if (error.empty()) error = "已安装应用校验失败";
    return false;
  }
  return true;
}
} // namespace lunaredu::lunaros::apps::runtime_detail
