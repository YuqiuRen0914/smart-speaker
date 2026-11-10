#include "ota_install_intent.h"
#include "configuration/settings.h"
#include "recovery_contract.h"
#include <sdkconfig.h>
#include <cstring>
#include <cstdio>
#include <mutex>

namespace lunaredu::lunaros {
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
namespace {
struct IntentRecord {
  uint32_t magic = 0x3149544f;
  uint32_t schema = 1;
  uint32_t active = 0;
  uint32_t attempts = 0;
  uint32_t size = 0;
  char url[1025]{};
  char sha256[65]{};
  uint32_t crc = 0;
};
std::mutex intent_mutex;
bool store(IntentRecord &record, std::string &error) {
  record.crc = recovery::checksum(&record, offsetof(IntentRecord, crc));
  return settings_write_blob("ota_intent", "install",
      {reinterpret_cast<const char *>(&record), sizeof(record)}, error, "recovery_state");
}
bool load(IntentRecord &record, std::string &error) {
  if (!initialize_settings_partition("recovery_state", error)) return false;
  std::string data;
  const auto result = settings_read_blob("ota_intent", "install", sizeof(record),
                                         data, error, "recovery_state");
  if (result == SettingsReadResult::Missing) { error.clear(); return true; }
  if (result != SettingsReadResult::Found) return false;
  if (data.size() != sizeof(record)) { error = "ota_intent_size_invalid"; return false; }
  std::memcpy(&record, data.data(), sizeof(record));
  if (record.magic != 0x3149544f || record.schema != 1 || record.active > 1 ||
      record.attempts > 3 || record.url[1024] || record.sha256[64] ||
      record.crc != recovery::checksum(&record, offsetof(IntentRecord, crc))) {
    error = "ota_intent_corrupt"; return false;
  }
  error.clear(); return true;
}
}
bool ota_intent_stage(const OtaInstallIntent &intent, std::string &error) {
  std::lock_guard<std::mutex> lock(intent_mutex);
  bool valid_hash = intent.sha256.size() == 64;
  for (char c : intent.sha256)
    valid_hash = valid_hash && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
  if (intent.url.rfind("https://", 0) != 0 || intent.url.size() > 1024 ||
      intent.url.find('\0') != std::string::npos || !valid_hash ||
      intent.size == 0 || intent.size > 0x600000) {
    error = "ota_intent_invalid"; return false;
  }
  if (!initialize_settings_partition("recovery_state", error)) return false;
  IntentRecord record;
  record.active = 1;
  record.size = static_cast<uint32_t>(intent.size);
  std::snprintf(record.url, sizeof(record.url), "%s", intent.url.c_str());
  std::snprintf(record.sha256, sizeof(record.sha256), "%s", intent.sha256.c_str());
  return store(record, error);
}
bool ota_intent_begin_attempt(OtaInstallIntent &intent, std::string &error) {
  std::lock_guard<std::mutex> lock(intent_mutex);
  intent = {};
  IntentRecord record;
  if (!load(record, error)) return false;
  if (!record.active) { error.clear(); return false; }
  if (record.attempts >= 3) {
    record.active = 0;
    std::memset(record.url, 0, sizeof(record.url));
    if (!store(record, error)) return false;
    error = "ota_intent_attempts_exhausted"; return false;
  }
  ++record.attempts; // commit before dispatch, bounding repeated power losses.
  if (!store(record, error)) return false;
  intent = {record.url, record.sha256, record.size};
  return true;
}
bool ota_intent_clear(std::string &error) {
  std::lock_guard<std::mutex> lock(intent_mutex);
  if (!initialize_settings_partition("recovery_state", error)) return false;
  IntentRecord empty;
  return store(empty, error);
}
void ota_intent_finish(const std::string &sha256, bool) {
  std::lock_guard<std::mutex> lock(intent_mutex);
  std::string error;
  IntentRecord record;
  if (!load(record, error) || !record.active || sha256 != record.sha256) return;
  // Retain no URL/ticket after a terminal result. Journal owns the result.
  record = {};
  (void)store(record, error);
}
#else
bool ota_intent_stage(const OtaInstallIntent &, std::string &error) {
  error = "recovery_layout_disabled"; return false;
}
bool ota_intent_begin_attempt(OtaInstallIntent &, std::string &error) { error.clear(); return false; }
bool ota_intent_clear(std::string &error) { error.clear(); return true; }
void ota_intent_finish(const std::string &, bool) {}
#endif
}
