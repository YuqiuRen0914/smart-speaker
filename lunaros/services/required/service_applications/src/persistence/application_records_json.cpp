#include "app/application_persistence.h"
#include "app/package/application_limits.h"
#include <ArduinoJson.h>

namespace lunaredu::lunaros::apps {
bool decode_installed_catalog(const std::string &text, std::vector<Application> &apps) {
  JsonDocument doc;
  if (text.size() > kMaximumCatalogBytes || deserializeJson(doc, text) ||
      !doc.is<JsonArray>() || doc.size() > kMaximumApplications) return false;
  std::vector<Application> next;
  for (JsonVariantConst value : doc.as<JsonArrayConst>()) {
    if (!value.is<JsonObjectConst>()) return false;
    auto item = value.as<JsonObjectConst>();
    Application a;
    a.id=item["id"]|"";a.name=item["name"]|"";a.version=item["version"]|"";
    a.description=item["description"]|"";a.color=item["color"]|uint16_t(0xfdcd);a.size=item["size"]|0U;
    a.backend=item["backend"]|"";
    a.artifact_id=item["artifact_id"]|"";a.entry_format=item["entry_format"]|"";
    a.source_sha256=item["source_sha256"]|"";a.entry_sha256=item["entry_sha256"]|"";
    a.package_version=item["package_version"]|0U;
    a.signer_id=item["signer_id"]|"";a.package_signature=item["package_signature"]|"";
    a.sha256=item["sha256"]|"";a.tick_hz=item["tick_hz"]|0;a.icon_resource=item["icon_resource"]|"";
    if (!item["private_storage"].is<bool>()) return false;
    a.private_storage=item["private_storage"].as<bool>();
    if(!item["device_basic"].isNull()&&!item["device_basic"].is<bool>())return false;
    a.device_basic=item["device_basic"]|false;
    next.push_back(std::move(a));
  }
  apps=std::move(next);
  return true;
}
bool encode_installed_catalog(const std::vector<Application> &apps, std::string &text) {
  JsonDocument doc;auto array=doc.to<JsonArray>();
  for(const auto &a:apps){auto json=array.add<JsonObject>();json["id"]=a.id;json["name"]=a.name;json["version"]=a.version;json["description"]=a.description;json["color"]=a.color;json["size"]=a.size;json["sha256"]=a.sha256;json["tick_hz"]=a.tick_hz;json["icon_resource"]=a.icon_resource;json["private_storage"]=a.private_storage;json["backend"]=a.backend;
    json["device_basic"]=a.device_basic;
    json["artifact_id"]=a.artifact_id;json["entry_format"]=a.entry_format;json["source_sha256"]=a.source_sha256;
    json["entry_sha256"]=a.entry_sha256;json["package_version"]=a.package_version;
    if(a.entry_format=="aot"){json["signer_id"]=a.signer_id;json["package_signature"]=a.package_signature;}}
  text.clear();serializeJson(doc,text);
  return text.size()<=kMaximumCatalogBytes;
}
std::string encode_save_metadata(const ApplicationSaveMetadata &metadata) {
  JsonDocument doc;doc["timestamp"]=metadata.timestamp;doc["hash"]=metadata.hash;
  std::string text;serializeJson(doc,text);return text;
}
bool decode_save_metadata(const std::vector<uint8_t> &bytes, ApplicationSaveMetadata &metadata) {
  JsonDocument doc;
  if (deserializeJson(doc,bytes.data(),bytes.size())) return false;
  metadata.timestamp=doc["timestamp"]|0U;
  metadata.hash=doc["hash"]|0U;
  return true;
}
} // namespace lunaredu::lunaros::apps
