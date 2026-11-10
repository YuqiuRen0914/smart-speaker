#include "configuration/settings.h"
#include "storage/nvs_backend.h"
#include <cstring>

namespace lunaredu::lunaros {
namespace {
SettingsReadResult result(NvsReadResult value, std::string &error) {
  if (value == NvsReadResult::Found) { error.clear(); return SettingsReadResult::Found; }
  if (value == NvsReadResult::Missing) { error = "settings_missing"; return SettingsReadResult::Missing; }
  if (error.empty()) error = "settings_read_failed";
  return SettingsReadResult::Failed;
}
}
bool initialize_settings(std::string &error) { return nvs_store_initialize(&error, true); }
bool initialize_settings_partition(const char *partition, std::string &error) {
  return initialize_nvs_partition(partition, error);
}
SettingsReadResult settings_read_u8(const char *ns, const char *key, uint8_t &value, std::string &error) {
  value = 0;
  if (!setup_nvs_store(&error)) return SettingsReadResult::Failed;
  return result(nvs_store_read_u8(ns, key, value, &error), error);
}
SettingsReadResult settings_read_i8(const char *ns, const char *key, int8_t &value, std::string &error) {
  value = 0;
  if (!setup_nvs_store(&error)) return SettingsReadResult::Failed;
  return result(nvs_store_read_i8(ns, key, value, &error), error);
}
SettingsReadResult settings_read_u32(const char *ns, const char *key, uint32_t &value, std::string &error) {
  value = 0;
  if (!setup_nvs_store(&error)) return SettingsReadResult::Failed;
  return result(nvs_store_read_u32(ns, key, value, &error), error);
}
bool settings_write_u8(const char *ns, const char *key, uint8_t value, std::string &error) {
  const SettingsWrite entry{key, SettingsValueType::U8, nullptr, 0, value};
  return settings_write_batch(ns, &entry, 1, error);
}
SettingsReadResult settings_read_i32(const char *ns, const char *key, int32_t &value, std::string &error) {
  value = 0;
  if (!setup_nvs_store(&error)) return SettingsReadResult::Failed;
  return result(nvs_store_read_i32(ns, key, value, &error), error);
}
bool settings_write_i32(const char *ns, const char *key, int32_t value, std::string &error) {
  const SettingsWrite entry{key, SettingsValueType::I32, nullptr, 0, static_cast<uint32_t>(value)};
  return settings_write_batch(ns, &entry, 1, error);
}
bool settings_write_i8(const char *ns, const char *key, int8_t value, std::string &error) {
  const auto entry = SettingsWrite::i8(key, value);
  return settings_write_batch(ns, &entry, 1, error);
}
bool settings_write_u32(const char *ns, const char *key, uint32_t value, std::string &error) {
  const SettingsWrite entry{key, SettingsValueType::U32, nullptr, 0, value};
  return settings_write_batch(ns, &entry, 1, error);
}
SettingsReadResult settings_read_blob(const char *ns, const char *key, void *output,
    size_t capacity, size_t &actual, std::string &error) {
  actual = 0;
  if (!output || !capacity) { error = "invalid_settings_size"; return SettingsReadResult::Failed; }
  if (!setup_nvs_store(&error)) {
    std::memset(output, 0, capacity); return SettingsReadResult::Failed;
  }
  return result(nvs_store_read_blob_into(ns, key, output, capacity, actual, &error), error);
}
SettingsReadResult settings_read_blob(const char *ns, const char *key, size_t maximum,
    std::string &value, std::string &error, const char *partition) {
  value.clear();
  if (!maximum) { error = "invalid_settings_size"; return SettingsReadResult::Failed; }
  if (!partition || !*partition) { error = "invalid_settings_partition"; return SettingsReadResult::Failed; }
  return result(nvs_store_read_blob(ns, key, maximum, value, &error, partition), error);
}
bool settings_write_blob(const char *ns, const char *key, const std::string &value,
    std::string &error, const char *partition) {
  if (value.empty()) { error = "invalid_settings_size"; return false; }
  if (!partition || !*partition) { error = "invalid_settings_partition"; return false; }
  return nvs_store_write_blob(ns, key, value, &error, partition);
}
bool settings_erase_namespace(const char *ns, std::string &error) {
  return setup_nvs_store(&error) && nvs_store_erase_namespace(ns, &error);
}
bool settings_write_batch(const char *ns, const SettingsWrite *writes, size_t count, std::string &error) {
  // Admission is checked before initialization and before any storage mutation.
  if (!nvs_store_validate_batch(ns, writes, count, &error)) return false;
  return setup_nvs_store(&error) && nvs_store_write_batch(ns, writes, count, &error);
}
} // namespace lunaredu::lunaros
