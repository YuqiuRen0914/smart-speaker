#include "lunaros_network_espnow.h"
#include "lunaros_network_espnow_binding.h"

#include <cstdio>
#include <cstring>

#include "configuration/settings.h"
#include <esp_err.h>
#include <esp_random.h>

namespace lunaredu::lunaros::espnow_binding {

BindingReadResult load_binding(const char *name_space, Binding &binding, std::string &error,
                  const char *key) {
  binding = {};
  error.clear();
  if (!name_space || !*name_space || !key || !*key) {
    error = "invalid_binding_key";
    return BindingReadResult::Failed;
  }
  if (!lunaredu::lunaros::initialize_settings(error))
    return BindingReadResult::Failed;
  Binding loaded{};size_t size=0;
  const auto result=lunaredu::lunaros::settings_read_blob(name_space,key,&loaded,sizeof(loaded),size,error);
  if(result==lunaredu::lunaros::SettingsReadResult::Missing){error.clear();return BindingReadResult::Missing;}
  if(result!=lunaredu::lunaros::SettingsReadResult::Found)return BindingReadResult::Failed;
  if(size!=sizeof(loaded)||!binding_valid(loaded)){error="invalid_binding";return BindingReadResult::Failed;}
  binding = loaded;
  error.clear();
  return BindingReadResult::Found;
}

bool save_binding(const char *name_space, const Binding &binding,
                  std::string &error, const char *key) {
  if(!binding_valid(binding)){error="invalid_binding";return false;}
  if(!lunaredu::lunaros::initialize_settings(error))return false;
  const auto entry=lunaredu::lunaros::SettingsWrite::blob(key,&binding,sizeof(binding));
  return lunaredu::lunaros::settings_write_batch(name_space,&entry,1,error);
}

bool clear_binding(const char *name_space, std::string &error,
                   const char *key) {
  if(!lunaredu::lunaros::initialize_settings(error))return false;
  return lunaredu::lunaros::settings_erase(name_space,key,error);
}

uint64_t make_binding_nonce() {
  uint64_t nonce = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
  return nonce == 0 ? 1 : nonce;
}

}  // namespace lunaredu::lunaros::espnow_binding
