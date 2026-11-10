#include "storage/nvs_backend.h"
#include "settings/settings_key.h"

#include "configuration/settings.h"
#include "kernel/platform_operation.h"
#include <cstring>
#include <limits>
#include <atomic>
#include <esp_err.h>
#include <nvs.h>
#include <utility>

#include <esp_log.h>

namespace lunaredu::lunaros {

namespace {

std::atomic<bool> g_nvs_ready{false};
void set_error(std::string *output, const char *message) {
  if (output) *output = message;
}
bool prepare(const char *name_space, const char *key, std::string *error,
             const char *partition = nullptr) {
  set_error(error, "");
  if (!g_nvs_ready && !partition) { set_error(error, "nvs_not_ready"); return false; }
  if (partition && !*partition) { set_error(error, "invalid_nvs_partition"); return false; }
  if (!name_space || !*name_space || !key || !*key) {
    set_error(error, "invalid_nvs_key"); return false;
  }
  return true;
}

} // 匿名命名空间

bool setup_nvs_store(std::string *operation_error) {
  return nvs_store_initialize(operation_error, false);
}

bool nvs_store_initialize(std::string *operation_error, bool force) {
  set_error(operation_error, "");
  if (g_nvs_ready && !force) return true;
  PlatformOperation operation(execution_effect(ExecutionEffect::FlashWrite));
  if(!operation.accepted()) {set_error(operation_error,"nvs_execution_not_admitted");return false;}
  std::string error;
  if (!initialize_nvs(error)) {
    set_error(operation_error, error.empty() ? "nvs_initialization_failed" : error.c_str());
    ESP_LOGE("config.nvs", "nvs init failed: %s", error.c_str());
    return false;
  }
  g_nvs_ready = true;
  return true;
}

bool nvs_store_ready() { return g_nvs_ready; }

NvsReadResult nvs_store_read_u16(const char *name_space, const char *key,
                                uint16_t &value, std::string *operation_error) {
  value = 0;
  if (!prepare(name_space, key, operation_error)) return NvsReadResult::Failed;
  nvs_handle_t handle = 0;
  const auto stored_key = internal::normalize_nvs_key(key);
  uint16_t loaded = 0;
  esp_err_t result = nvs_open(name_space, NVS_READONLY, &handle);
  if (result == ESP_OK) {
    result = nvs_get_u16(handle, stored_key.c_str(), &loaded);
    nvs_close(handle);
  }
  set_error(operation_error, result == ESP_OK ? "" : esp_err_to_name(result));
  if (result == ESP_ERR_NVS_NOT_FOUND) return NvsReadResult::Missing;
  if (result != ESP_OK) return NvsReadResult::Failed;
  value = loaded;
  return NvsReadResult::Found;
}

bool nvs_store_write_u16(const char *name_space, const char *key, uint16_t value, std::string *operation_error) {
  PlatformOperation operation(execution_effect(ExecutionEffect::FlashWrite));
  if (!operation.accepted()) return false;
  if (!prepare(name_space, key, operation_error)) return false;
  nvs_handle_t handle = 0;
  const auto stored_key = internal::normalize_nvs_key(key);
  esp_err_t result = nvs_open(name_space, NVS_READWRITE, &handle);
  if (result == ESP_OK) {
    result = nvs_set_u16(handle, stored_key.c_str(), value);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
  }
  set_error(operation_error, result == ESP_OK ? "" : esp_err_to_name(result));
  return result == ESP_OK;
}

NvsReadResult nvs_store_read_blob(const char *namespace_name, const char *key,
                                size_t maximum_bytes, std::string &value, std::string *error) {
  return nvs_store_read_blob(namespace_name, key, maximum_bytes, value, error, nullptr);
}
bool nvs_store_write_blob(const char *namespace_name, const char *key,
                          const std::string &value, std::string *error) {
  PlatformOperation operation(execution_effect(ExecutionEffect::FlashWrite));
  if (!operation.accepted()) return false;
  return nvs_store_write_blob(namespace_name, key, value, error, nullptr);
}

NvsReadResult nvs_store_read_blob(const char *namespace_name, const char *key,
                                     size_t maximum_bytes, std::string &value, std::string *operation_error,
                                     const char *partition) {
  value.clear();
  if (!prepare(namespace_name, key, operation_error, partition)) return NvsReadResult::Failed;
  if (maximum_bytes == 0) {
    set_error(operation_error, "invalid_nvs_size"); return NvsReadResult::Failed;
  }
  nvs_handle_t handle = 0;
  const std::string stored_key = internal::normalize_nvs_key(key);
  esp_err_t error = partition ? nvs_open_from_partition(partition, namespace_name, NVS_READONLY, &handle)
                              : nvs_open(namespace_name, NVS_READONLY, &handle);
  if (error != ESP_OK) {
    set_error(operation_error, esp_err_to_name(error));
    return error == ESP_ERR_NVS_NOT_FOUND ? NvsReadResult::Missing
                                         : NvsReadResult::Failed;
  }
  size_t size = 0;
  error = nvs_get_blob(handle, stored_key.c_str(), nullptr, &size);
  if (error == ESP_ERR_NVS_NOT_FOUND) {
    set_error(operation_error, esp_err_to_name(error));
    nvs_close(handle);
    return NvsReadResult::Missing;
  }
  if (error == ESP_OK && (size == 0 || size > maximum_bytes))
    error = ESP_ERR_NVS_INVALID_LENGTH;
  std::string buffer;
  if (error == ESP_OK) {
    buffer.resize(size);
    error = nvs_get_blob(handle, stored_key.c_str(), buffer.data(), &size);
    if (error == ESP_OK && (size == 0 || size > buffer.size()))
      error = ESP_ERR_NVS_INVALID_LENGTH;
    if (error == ESP_OK) buffer.resize(size);
  }
  nvs_close(handle);
  set_error(operation_error, error == ESP_OK ? "" : esp_err_to_name(error));
  if (error != ESP_OK) return NvsReadResult::Failed;
  value = std::move(buffer);
  return NvsReadResult::Found;
}

bool nvs_store_write_blob(const char *namespace_name, const char *key,
                          const std::string &value, std::string *operation_error,
                          const char *partition) {
  PlatformOperation operation(execution_effect(ExecutionEffect::FlashWrite));
  if (!operation.accepted()) return false;
  if (!prepare(namespace_name, key, operation_error, partition)) return false;
  if (value.empty()) { set_error(operation_error, "invalid_nvs_size"); return false; }
  nvs_handle_t handle = 0;
  const std::string stored_key = internal::normalize_nvs_key(key);
  esp_err_t error = partition ? nvs_open_from_partition(partition, namespace_name, NVS_READWRITE, &handle)
                              : nvs_open(namespace_name, NVS_READWRITE, &handle);
  if (error == ESP_OK) {
    error = nvs_set_blob(handle, stored_key.c_str(), value.data(), value.size());
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
  }
  set_error(operation_error, error == ESP_OK ? "" : esp_err_to_name(error));
  return error == ESP_OK;
}

NvsReadResult nvs_store_read_string_bounded(const char *namespace_name, const char *key,
                                            size_t maximum_bytes, std::string &value, std::string *operation_error) {
  value.clear();
  if (!prepare(namespace_name, key, operation_error)) {
    return NvsReadResult::Failed;
  }

  const std::string stored_key = internal::normalize_nvs_key(key);
  nvs_handle_t handle = 0;
  esp_err_t error = nvs_open(namespace_name, NVS_READONLY, &handle);
  if (error != ESP_OK) {
    set_error(operation_error, esp_err_to_name(error));
    return error == ESP_ERR_NVS_NOT_FOUND ? NvsReadResult::Missing : NvsReadResult::Failed;
  }

  size_t required_size = 0;
  error = nvs_get_str(handle, stored_key.c_str(), nullptr, &required_size);
  if (error == ESP_OK && (required_size == 0 || required_size - 1 > maximum_bytes))
    error = ESP_ERR_NVS_INVALID_LENGTH;
  if (error != ESP_OK) {
    nvs_close(handle);
    set_error(operation_error, esp_err_to_name(error));
    return error == ESP_ERR_NVS_NOT_FOUND ? NvsReadResult::Missing : NvsReadResult::Failed;
  }

  std::string buffer(required_size, '\0');
  error = nvs_get_str(handle, stored_key.c_str(), buffer.data(), &required_size);
  nvs_close(handle);
  if (error != ESP_OK) {
    set_error(operation_error, esp_err_to_name(error));
    return NvsReadResult::Failed;
  }

  if (required_size == 0 || required_size > buffer.size() || buffer[required_size - 1] != '\0') {
    set_error(operation_error, esp_err_to_name(ESP_ERR_NVS_INVALID_LENGTH));
    return NvsReadResult::Failed;
  }
  buffer.resize(required_size - 1);
  value = std::move(buffer);
  set_error(operation_error, "");
  return NvsReadResult::Found;
}

bool nvs_store_write_string(const char *namespace_name, const char *key,
                            const std::string &value, std::string *operation_error) {
  PlatformOperation operation(execution_effect(ExecutionEffect::FlashWrite));
  if (!operation.accepted()) return false;
  if (!prepare(namespace_name, key, operation_error)) {
    return false;
  }

  const std::string stored_key = internal::normalize_nvs_key(key);
  nvs_handle_t handle = 0;
  esp_err_t error = nvs_open(namespace_name, NVS_READWRITE, &handle);
  if (error != ESP_OK) {
    set_error(operation_error, esp_err_to_name(error));
    return false;
  }

  error = nvs_set_str(handle, stored_key.c_str(), value.c_str());
  if (error == ESP_OK) {
    error = nvs_commit(handle);
  }
  nvs_close(handle);
  set_error(operation_error, error == ESP_OK ? "" : esp_err_to_name(error));
  return error == ESP_OK;
}

NvsReadResult nvs_store_read_bool(const char *namespace_name, const char *key,
                                 bool &value, std::string *operation_error) {
  value = false;
  if (!prepare(namespace_name, key, operation_error)) {
    return NvsReadResult::Failed;
  }
  const auto stored_key = internal::normalize_nvs_key(key);
  nvs_handle_t handle = 0;
  uint8_t stored = 0;
  esp_err_t error = nvs_open(namespace_name, NVS_READONLY, &handle);
  if (error == ESP_OK) {
    error = nvs_get_u8(handle, stored_key.c_str(), &stored);
    nvs_close(handle);
  }
  set_error(operation_error, error == ESP_OK ? "" : esp_err_to_name(error));
  if (error == ESP_ERR_NVS_NOT_FOUND) return NvsReadResult::Missing;
  if (error != ESP_OK) return NvsReadResult::Failed;
  value = stored != 0;
  return NvsReadResult::Found;
}

bool nvs_store_write_bool(const char *namespace_name, const char *key,
                          bool value, std::string *operation_error) {
  PlatformOperation operation(execution_effect(ExecutionEffect::FlashWrite));
  if (!operation.accepted()) return false;
  if (!prepare(namespace_name, key, operation_error)) {
    return false;
  }

  const std::string stored_key = internal::normalize_nvs_key(key);
  nvs_handle_t handle = 0;
  esp_err_t error = nvs_open(namespace_name, NVS_READWRITE, &handle);
  if (error != ESP_OK) {
    set_error(operation_error, esp_err_to_name(error));
    return false;
  }

  error = nvs_set_u8(handle, stored_key.c_str(), value ? 1 : 0);
  if (error == ESP_OK) {
    error = nvs_commit(handle);
  }
  nvs_close(handle);
  set_error(operation_error, error == ESP_OK ? "" : esp_err_to_name(error));
  return error == ESP_OK;
}

bool nvs_store_erase_key(const char *namespace_name, const char *key, std::string *operation_error) {
  PlatformOperation operation(execution_effect(ExecutionEffect::FlashWrite));
  if (!operation.accepted()) return false;
  if (!prepare(namespace_name, key, operation_error)) {
    return false;
  }

  const std::string stored_key = internal::normalize_nvs_key(key);
  nvs_handle_t handle = 0;
  esp_err_t error = nvs_open(namespace_name, NVS_READWRITE, &handle);
  if (error != ESP_OK) {
    set_error(operation_error, esp_err_to_name(error));
    return false;
  }

  error = nvs_erase_key(handle, stored_key.c_str());
  if (error == ESP_ERR_NVS_NOT_FOUND) {
    error = ESP_OK;
  }
  if (error == ESP_OK) {
    error = nvs_commit(handle);
  }
  nvs_close(handle);
  set_error(operation_error, error == ESP_OK ? "" : esp_err_to_name(error));
  return error == ESP_OK;
}


namespace {
template<class T, class Read> NvsReadResult read_integer(const char *ns, const char *key,
    T &value, std::string *error, Read read) {
  value = 0;
  if (!prepare(ns, key, error)) return NvsReadResult::Failed;
  nvs_handle_t handle = 0;
  auto code = nvs_open(ns, NVS_READONLY, &handle);
  T loaded{};
  if (code == ESP_OK) {
    code = read(handle, internal::normalize_nvs_key(key).c_str(), &loaded);
    nvs_close(handle);
  }
  set_error(error, code == ESP_OK ? "" : esp_err_to_name(code));
  if (code == ESP_ERR_NVS_NOT_FOUND) return NvsReadResult::Missing;
  if (code != ESP_OK) return NvsReadResult::Failed;
  value = loaded; return NvsReadResult::Found;
}
}
NvsReadResult nvs_store_read_u8(const char *ns, const char *key, uint8_t &v, std::string *e) {
  return read_integer(ns,key,v,e,nvs_get_u8);
}
NvsReadResult nvs_store_read_i8(const char *ns, const char *key, int8_t &v, std::string *e) {
  return read_integer(ns,key,v,e,nvs_get_i8);
}
NvsReadResult nvs_store_read_u32(const char *ns, const char *key, uint32_t &v, std::string *e) {
  return read_integer(ns,key,v,e,nvs_get_u32);
}
NvsReadResult nvs_store_read_i32(const char *ns, const char *key, int32_t &v, std::string *e) {
  return read_integer(ns,key,v,e,nvs_get_i32);
}
NvsReadResult nvs_store_read_blob_into(const char *ns, const char *key, void *output,
    size_t capacity, size_t &actual, std::string *error) {
  actual = 0;
  if (!output || !capacity) { set_error(error,"invalid_settings_size"); return NvsReadResult::Failed; }
  std::memset(output,0,capacity);
  if (!prepare(ns,key,error)) return NvsReadResult::Failed;
  nvs_handle_t handle = 0;
  auto code = nvs_open(ns,NVS_READONLY,&handle);
  size_t size = capacity;
  if (code == ESP_OK) {
    code = nvs_get_blob(handle,internal::normalize_nvs_key(key).c_str(),output,&size);
    nvs_close(handle);
  }
  set_error(error,code == ESP_OK ? "" : esp_err_to_name(code));
  if (code == ESP_OK && size <= capacity) { actual=size; return NvsReadResult::Found; }
  std::memset(output,0,capacity);
  return code == ESP_ERR_NVS_NOT_FOUND ? NvsReadResult::Missing : NvsReadResult::Failed;
}
bool nvs_store_erase_namespace(const char *ns, std::string *error) {
  PlatformOperation operation(execution_effect(ExecutionEffect::FlashWrite));
  if (!operation.accepted()) return false;
  if (!prepare(ns,"_",error)) return false;
  nvs_handle_t handle = 0;
  auto code = nvs_open(ns,NVS_READWRITE,&handle);
  if (code == ESP_OK) { code=nvs_erase_all(handle); if(code==ESP_OK)code=nvs_commit(handle); nvs_close(handle); }
  set_error(error,code == ESP_OK ? "" : esp_err_to_name(code));
  return code == ESP_OK;
}
bool nvs_store_validate_batch(const char *ns, const SettingsWrite *writes,
    size_t count, std::string *error) {
  set_error(error,"");
  if (!ns || !*ns || !writes || !count || count>kSettingsBatchCapacity) {
    set_error(error,"invalid_settings_batch"); return false;
  }
  for (size_t i=0;i<count;++i) {
    const auto &w=writes[i];
    if (!w.key || !*w.key) { set_error(error,"invalid_settings_key"); return false; }
    for(size_t j=0;j<i;++j) if(internal::normalize_nvs_key(w.key)==internal::normalize_nvs_key(writes[j].key)) {
      set_error(error,"duplicate_settings_key"); return false;
    }
    switch(w.type) {
    case SettingsValueType::Blob:
      if (!w.data || !w.size) { set_error(error,"invalid_settings_size"); return false; } break;
    case SettingsValueType::String:
      if ((!w.data && w.size) || (w.size && std::memchr(w.data,0,w.size))) {
        set_error(error,"invalid_settings_string"); return false;
      } break;
    case SettingsValueType::U8: case SettingsValueType::I8:
      if (w.integer>UINT8_MAX) { set_error(error,"invalid_settings_integer"); return false; } break;
    case SettingsValueType::U16:
      if (w.integer>UINT16_MAX) { set_error(error,"invalid_settings_integer"); return false; } break;
    case SettingsValueType::U32: case SettingsValueType::I32: break;
    default: set_error(error,"invalid_settings_type"); return false;
    }
  }
  return true;
}
bool nvs_store_write_batch(const char *ns, const SettingsWrite *writes,
    size_t count, std::string *error) {
  PlatformOperation operation(execution_effect(ExecutionEffect::FlashWrite));
  if (!operation.accepted()) return false;
  if (!nvs_store_validate_batch(ns,writes,count,error) || !prepare(ns,writes[0].key,error))return false;
  nvs_handle_t handle = 0;
  auto code=nvs_open(ns,NVS_READWRITE,&handle);
  if(code==ESP_OK) {
    for(size_t i=0;i<count && code==ESP_OK;++i) {
      const auto &w=writes[i]; const auto key=internal::normalize_nvs_key(w.key);
      switch(w.type) {
      case SettingsValueType::Blob: code=nvs_set_blob(handle,key.c_str(),w.data,w.size); break;
      case SettingsValueType::String: {
        const std::string value(w.data ? static_cast<const char *>(w.data) : "",w.size);
        code=nvs_set_str(handle,key.c_str(),value.c_str()); break;
      }
      case SettingsValueType::U8: code=nvs_set_u8(handle,key.c_str(),static_cast<uint8_t>(w.integer)); break;
      case SettingsValueType::I8: code=nvs_set_i8(handle,key.c_str(),static_cast<int8_t>(static_cast<uint8_t>(w.integer))); break;
      case SettingsValueType::U16: code=nvs_set_u16(handle,key.c_str(),static_cast<uint16_t>(w.integer)); break;
      case SettingsValueType::I32: code=nvs_set_i32(handle,key.c_str(),static_cast<int32_t>(w.integer)); break;
      case SettingsValueType::U32: code=nvs_set_u32(handle,key.c_str(),w.integer); break;
      }
    }
    if(code==ESP_OK)code=nvs_commit(handle);
    nvs_close(handle);
  }
  set_error(error,code==ESP_OK ? "" : esp_err_to_name(code)); return code==ESP_OK;
}
} // namespace lunaredu::lunaros
