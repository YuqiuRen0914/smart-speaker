#include "configuration/settings.h"
#include "storage/nvs_backend.h"

namespace lunaredu::lunaros {
namespace {
bool prepare(const char *name_space, const char *key, std::string &error) {
  error.clear();
  if (!name_space || !*name_space || !key || !*key) {
    error = "invalid_settings_key";
    return false;
  }
  if (!setup_nvs_store(&error)) {
    if (error.empty()) error = "settings_initialization_failed";
    return false;
  }
  return true;
}
SettingsReadResult read_result(NvsReadResult result, std::string &error) {
  if (result == NvsReadResult::Found) { error.clear(); return SettingsReadResult::Found; }
  if (result == NvsReadResult::Missing) { error = "settings_missing"; return SettingsReadResult::Missing; }
  if (error.empty()) error = "settings_read_failed";
  return SettingsReadResult::Failed;
}
bool write_result(bool ok, std::string &error) {
  if (ok) error.clear();
  if (!ok && error.empty()) error = "settings_write_failed";
  return ok;
}
}
SettingsReadResult settings_read_string(const char *name_space, const char *key,
                                        size_t maximum_bytes, std::string &value,
                                        std::string &error) {
  value.clear();
  if (maximum_bytes == 0 || maximum_bytes == SIZE_MAX) {
    error = "invalid_settings_size";
    return SettingsReadResult::Failed;
  }
  if (!prepare(name_space, key, error)) return SettingsReadResult::Failed;
  return read_result(nvs_store_read_string_bounded(name_space, key,
                                                  maximum_bytes, value, &error), error);
}
SettingsReadResult settings_read_bool(const char *name_space, const char *key,
                                      bool &value, std::string &error) {
  value = false;
  if (!prepare(name_space, key, error)) return SettingsReadResult::Failed;
  return read_result(nvs_store_read_bool(name_space, key, value, &error), error);
}
SettingsReadResult settings_read_u16(const char *name_space, const char *key,
                                     uint16_t &value, std::string &error) {
  value = 0;
  if (!prepare(name_space, key, error)) return SettingsReadResult::Failed;
  return read_result(nvs_store_read_u16(name_space, key, value, &error), error);
}
bool settings_write_u16(const char *name_space, const char *key,
                        uint16_t value, std::string &error) {
  return prepare(name_space, key, error) &&
      write_result(nvs_store_write_u16(name_space, key, value, &error), error);
}
SettingsReadResult settings_read_blob(const char *name_space, const char *key,
                                      size_t maximum_bytes, std::string &value,
                                      std::string &error) {
  value.clear();
  if (maximum_bytes == 0) { error = "invalid_settings_size"; return SettingsReadResult::Failed; }
  if (!prepare(name_space, key, error)) return SettingsReadResult::Failed;
  return read_result(nvs_store_read_blob(name_space, key, maximum_bytes, value, &error), error);
}
bool settings_write_blob(const char *name_space, const char *key,
                         const std::string &value, std::string &error) {
  if (value.empty()) { error = "invalid_settings_size"; return false; }
  return prepare(name_space, key, error) &&
      write_result(nvs_store_write_blob(name_space, key, value, &error), error);
}
bool settings_erase(const char *name_space, const char *key, std::string &error) {
  return prepare(name_space, key, error) &&
      write_result(nvs_store_erase_key(name_space, key, &error), error);
}
}
