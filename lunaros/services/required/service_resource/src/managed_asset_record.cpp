#include "managed_asset_record.h"
#include "lunaros_storage.h"
#include <ArduinoJson.h>

namespace lunaredu::lunaros::internal {
namespace {
bool read_record(const char *id,const std::string &path, ManagedAssetRecord &record, std::string *source = nullptr) {
  record = {};
  std::string text;
  auto collect = [](const uint8_t *data, size_t bytes, void *opaque) {
    auto &target = *static_cast<std::string *>(opaque);
    if (bytes >= 4096 - target.size()) return false;
    target.append(reinterpret_cast<const char *>(data), bytes);
    return true;
  };
  JsonDocument doc;
  if (!storage_stream_file(path.c_str(), collect, &text) ||
      deserializeJson(doc, text) || doc["schema"].as<int>() != 1) return false;
  record.request = {id, doc["revision"].as<std::string>(), doc["url"].as<std::string>(),
      doc["sha256"].as<std::string>(), doc["bytes"].as<size_t>()};
  record.ready = doc["ready"].as<bool>();
  const auto rejected = doc["source_rejected"];
  const auto failure = doc["source_error"];
  const auto status = doc["source_http_status"];
  if (!rejected.isUnbound() || !failure.isUnbound() || !status.isUnbound()) {
    if (!rejected.is<bool>() || !rejected.as<bool>() || record.ready ||
        !failure.is<std::string>() || !status.is<int>()) return false;
    record.source_error = failure.as<std::string>();
    record.source_http_status = status.as<int>();
    const bool http = record.source_error == "asset_download_http_rejected" &&
        (record.source_http_status == 401 || record.source_http_status == 403 ||
         record.source_http_status == 404 || record.source_http_status == 410);
    const bool content = record.source_http_status == 0 &&
        (record.source_error == "asset_length_mismatch" ||
         record.source_error == "asset_verification_failed" ||
         record.source_error == "asset_format_invalid" ||
         record.source_error == "asset_compression_unavailable" ||
         record.source_error == "asset_download_cancelled");
    if (!http && !content) return false;
    record.source_rejected = true;
  }
  if (source) *source = doc["source"].as<std::string>();
  return true;
}

bool save_record(const std::string &path, const ManagedAssetRecord &record, const std::string *source = nullptr) {
  JsonDocument doc;
  doc["schema"] = 1;
  doc["revision"] = record.request.revision;
  doc["url"] = record.request.url;
  doc["sha256"] = record.request.sha256;
  doc["bytes"] = record.request.bytes;
  doc["ready"] = record.ready;
  if (record.source_rejected) {
    doc["source_rejected"] = true;
    doc["source_error"] = record.source_error;
    doc["source_http_status"] = record.source_http_status;
  }
  if (source) doc["source"] = *source;
  std::string text, error;
  serializeJson(doc, text);
  return text.size() < 4096 &&
      storage_write_text_file((path + ".tmp").c_str(), text.c_str()) &&
      storage_move_file((path + ".tmp").c_str(), path.c_str(), &error);
}
} // namespace
bool read_managed_asset_record(const char *id,ManagedAssetRecord &record) {
  return read_record(id,std::string("/managed/")+id+".json",record);
}
bool save_managed_asset_record(const char *id,const ManagedAssetRecord &record) {
  return save_record(std::string("/managed/")+id+".json",record);
}
bool read_pending_managed_asset_record(const char *id,ManagedAssetRecord &record) {
  return read_record(id,std::string("/managed/")+id+".pending.json",record);
}
bool save_pending_managed_asset_record(const char *id,const ManagedAssetRecord &record) {
  return save_record(std::string("/managed/")+id+".pending.json",record);
}
bool read_managed_asset_import_intent(const char *id, ManagedAssetImportIntent &intent) {
  intent = {};
  return read_record(id, std::string("/managed/")+id+".import.json", intent.record, &intent.source);
}
bool save_managed_asset_import_intent(const char *id, const ManagedAssetImportIntent &intent) {
  return save_record(std::string("/managed/")+id+".import.json", intent.record, &intent.source);
}
} // namespace lunaredu::lunaros::internal
