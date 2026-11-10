#include "delivery/resource/managed_asset_record.h"
#include "lunaredu_lunaros/persistence/storage.h"
#include <ArduinoJson.h>

namespace lunaredu::lunaros::internal {
bool read_managed_asset_record(const char *id, ManagedAssetRecord &record) {
  record = {};
  std::string text;
  auto collect = [](const uint8_t *data, size_t bytes, void *opaque) {
    auto &target = *static_cast<std::string *>(opaque);
    if (bytes >= 4096 - target.size()) return false;
    target.append(reinterpret_cast<const char *>(data), bytes);
    return true;
  };
  JsonDocument doc;
  if (!storage_stream_file((std::string("/managed/") + id + ".json").c_str(), collect, &text) ||
      deserializeJson(doc, text) || doc["schema"].as<int>() != 1) return false;
  record.request = {id, doc["revision"].as<std::string>(), doc["url"].as<std::string>(),
      doc["sha256"].as<std::string>(), doc["bytes"].as<size_t>()};
  record.ready = doc["ready"].as<bool>();
  return true;
}

bool save_managed_asset_record(const char *id, const ManagedAssetRecord &record) {
  JsonDocument doc;
  doc["schema"] = 1;
  doc["revision"] = record.request.revision;
  doc["url"] = record.request.url;
  doc["sha256"] = record.request.sha256;
  doc["bytes"] = record.request.bytes;
  doc["ready"] = record.ready;
  std::string text, error;
  serializeJson(doc, text);
  const auto base = std::string("/managed/") + id;
  return text.size() < 4096 &&
      storage_write_text_file((base + ".tmp").c_str(), text.c_str()) &&
      storage_move_file((base + ".tmp").c_str(), (base + ".json").c_str(), &error);
}
} // namespace lunaredu::lunaros::internal
