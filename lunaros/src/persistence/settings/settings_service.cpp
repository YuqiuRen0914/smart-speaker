#include "lunaredu_lunaros/persistence/settings.h"

#include "persistence/settings/nvs_store.h"

namespace lunaredu::lunaros {

std::string settings_get_string(const char *name_space, const char *key,
                                const char *default_value) {
  std::string value;
  if (!setup_nvs_store() ||
      !nvs_store_read_string(name_space, key, value)) {
    return default_value != nullptr ? default_value : "";
  }
  return value;
}

bool settings_set_string(const char *name_space, const char *key,
                         const std::string &value) {
  return setup_nvs_store() &&
         nvs_store_write_string(name_space, key, value);
}

bool settings_get_bool(const char *name_space, const char *key,
                       bool default_value) {
  bool value = default_value;
  if (!setup_nvs_store() || !nvs_store_read_bool(name_space, key, value)) {
    return default_value;
  }
  return value;
}

bool settings_set_bool(const char *name_space, const char *key, bool value) {
  return setup_nvs_store() && nvs_store_write_bool(name_space, key, value);
}

bool settings_erase(const char *name_space, const char *key) {
  return setup_nvs_store() && nvs_store_erase_key(name_space, key);
}

} // namespace lunaredu::lunaros
