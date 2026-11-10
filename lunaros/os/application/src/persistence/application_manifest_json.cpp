#include "persistence/application_manifest_json.h"
#include "package/application_manifest.h"
#include "package/application_manifest_codec.h"
#include "app/package/application_profile.h"
#include "persistence/application_artifact_internal.h"

namespace lunaredu::lunaros::apps {
bool decode_application_manifest(JsonVariantConst json, Application &app, std::string &error) {
  ApplicationManifestFacts facts;
  auto &value = facts.application;
  value.id = json["id"] | "";
  value.name = json["name"] | "";
  value.version = json["version"] | "";
  value.description = json["description"] | "";
  value.backend = json["backend"] | "";
  value.tick_hz = json["tick_hz"] | 0;
  value.icon_resource = json["icon"]["resource"] | "";
  facts.schema = json["schema"] | "";
  if (!json["abi"].is<uint32_t>() || !json["screen"]["width"].is<int>() ||
      !json["screen"]["height"].is<int>() ||
      (!json["memory_pages"].isNull() && !json["memory_pages"].is<uint32_t>())) {
    error = "应用与设备不兼容";
    return false;
  }
  facts.abi = json["abi"].as<uint32_t>();
  facts.canvas_width = json["screen"]["width"].as<int>();
  facts.canvas_height = json["screen"]["height"].as<int>();
  if (!json["memory_pages"].isNull()) facts.memory_pages = json["memory_pages"].as<uint32_t>();
  auto permissions = json["permissions"].as<JsonArrayConst>();
  if (permissions.isNull() || permissions.size() > std::size(kApplicationPermissions)) {
    error = "权限声明无效";
    return false;
  }
  for (auto permission : permissions) {
    const char *name = permission.as<const char *>();
    if (!name) { error = "权限声明无效"; return false; }
    facts.permissions.emplace_back(name);
  }
  if (!json["icon"]["color"].is<uint16_t>()) {
    error = "应用声明无效";
    return false;
  }
  facts.icon_kind = json["icon"]["kind"] | "";
  value.color = json["icon"]["color"].as<uint16_t>();
  return validate_application_manifest(facts, app, error);
}

bool decode_package_manifest(const uint8_t *bytes, size_t size, uint32_t version,
                             Application &app, std::string &error, bool aot) {
  JsonDocument doc;
  if (deserializeJson(doc, bytes, size) ||
      !decode_application_manifest(doc.as<JsonVariantConst>(), app, error) ||
      !validate_package_artifact(doc.as<JsonVariantConst>(), version, app, error, aot))
    return false;
  app.package_version = version;
  app.entry_format = doc["artifact"]["format"] | "";
  app.entry_sha256 = doc["entry_sha256"] | "";
  app.source_sha256 = doc["artifact"]["source_sha256"] | "";
  app.artifact_id = doc["artifact"]["id"] | "";
  return true;
}
} // namespace lunaredu::lunaros::apps
