#include "app/package/package_validation.h"
#include "package/application_manifest.h"
#include "package/application_manifest_codec.h"
#include "app/package/application_package_policy.h"
#include "app/execution/application_backend.h"
#include <cstring>
#include "app/package/package_format.h"
#include "lunaros_digest.h"
namespace lunaredu::lunaros::apps {
namespace {
constexpr bool application_permission_supported(std::string_view permission) {
  for (const auto allowed : kApplicationPermissions) if (allowed == permission) return true;
  return false;
}
}

std::string digest(const uint8_t *data, size_t size) {
  return ::lunaredu::lunaros::digest::sha256_hex(data, size);
}
bool validate_application_manifest(const ApplicationManifestFacts &facts, Application &output, std::string &error) {
  const auto &app = facts.application;
  bool private_storage = false,device_basic=false;
  const auto &profile = kApplicationPlatform;
  if(app.icon_resource.size()>profile.maximum_icon_resource_bytes){error="应用图标声明无效";return false;}
  if(app.tick_hz<0||app.tick_hz>profile.maximum_tick_hz){error="应用刷新频率无效";return false;}
  const auto *backend = application_backend_contract(app.backend);
  if (!id_valid(app.id) || !version_valid(app.version) || app.name.empty() ||
      app.name.size() > profile.maximum_name_bytes || app.description.size() > profile.maximum_description_bytes ||
      facts.schema != "lunaros.app/v1" ||
      !backend || facts.abi != profile.abi ||
      (backend->declares_memory_pages && facts.memory_pages != kApplicationMemoryPages) ||
      (!backend->declares_memory_pages && facts.memory_pages.has_value()) || facts.canvas_width != profile.canvas_width ||
      facts.canvas_height != profile.canvas_height) {
    error = "应用与设备不兼容";
    return false;
  }
  size_t characters = 0;
  for (unsigned char c : app.name)
    if ((c & 0xc0) != 0x80)
      characters++;
  if (characters > profile.maximum_name_characters) {
    error = "应用名称过长";
    return false;
  }
  bool graphics = false, input = false;
  if (facts.permissions.size() > std::size(kApplicationPermissions)) {
    error = "权限声明无效";
    return false;
  }
  for (const auto &permission : facts.permissions) {
    const char *p = permission.c_str();
    if (!application_permission_supported(p)) {
      error = "应用权限暂不支持";
      return false;
    }
    if ((!strcmp(p,"graphics")&&graphics)||(!strcmp(p,"input")&&input)||
        (!strcmp(p,"storage.private")&&private_storage)||(!strcmp(p,"device.basic")&&device_basic)) {
      error="应用权限重复";return false;
    }
    if (!strcmp(p, "graphics"))
      graphics = true;
    else if (!strcmp(p, "input"))
      input = true;
    else if (!strcmp(p, "storage.private"))
      private_storage = true;
    else if (!strcmp(p,"device.basic"))
      device_basic=true;
    else {
      error = "应用权限暂不支持";
      return false;
    }
  }
  if (!graphics || !input || facts.icon_kind != "games") {
    error = "应用声明无效";
    return false;
  }
  if(device_basic&&!backend->supports_device_basic){error="device.basic requires the Wasm host";return false;}
  output = app;
  output.private_storage = private_storage;
  output.device_basic=device_basic;
  return true;
}
bool unpack(PackageBytes bytes, Application &app, size_t &offset,
            std::string &error) {
  package::Envelope frame;
  if (!package::envelope(bytes.data(),bytes.size(),kApplicationPlatform.package_version,
      kMaximumPackageBytes,frame,error)) return false;
  offset=frame.entry_offset;
  const size_t jsonSize=frame.manifest_bytes, wasmSize=frame.entry_bytes;
  if (!decode_package_manifest(bytes.data() + 16, jsonSize, frame.version, app, error,
                               application_backend_available("aot"))) return false;
  const auto &expected = app.entry_sha256;
  if (!digest_valid(expected) ||
      digest(bytes.data() + offset, wasmSize) != expected ||
      !validate_application_entry(app.entry_format, bytes.data() + offset, wasmSize)) {
    error = "程序校验失败";
    return false;
  }
  return true;
}
}
