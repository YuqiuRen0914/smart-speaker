#include "configuration/settings.h"

#include "storage/nvs_backend.h"

namespace lunaredu::lunaros {


bool settings_set_string(const char *name_space, const char *key,
                         const std::string &value) {
  return setup_nvs_store() &&
         nvs_store_write_string(name_space, key, value);
}


bool settings_set_bool(const char *name_space, const char *key, bool value) {
  return setup_nvs_store() && nvs_store_write_bool(name_space, key, value);
}

bool settings_erase(const char *name_space, const char *key) {
  return setup_nvs_store() && nvs_store_erase_key(name_space, key);
}

} // namespace lunaredu::lunaros
