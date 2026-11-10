#include "persistence/settings/nvs_store.h"
#include "persistence/settings/settings_key.h"

#include "lunaredu_nvs.h"
#include <atomic>
#include <esp_err.h>
#include <nvs.h>
#include <utility>

#include "lunaredu_lunaros/kernel/log.h"

namespace lunaredu::lunaros {

namespace {

std::atomic<bool> g_nvs_ready{false};
std::atomic<const char *> g_nvs_last_error{""};

} // 匿名命名空间

bool setup_nvs_store() {
  if (g_nvs_ready) {
    return true;
  }

  std::string error;
  g_nvs_ready = lunaredu::nvs::initialize(error);
  // Keep the public error pointer stable; log the detailed initialization error.
  g_nvs_last_error = g_nvs_ready ? "" : "nvs_initialization_failed";
  if (!g_nvs_ready) {
    LOGE("config.nvs", "nvs init failed: %s", error.c_str());
  }
  return g_nvs_ready;
}

bool nvs_store_ready() { return g_nvs_ready; }

const char *nvs_store_last_error() { return g_nvs_last_error.load(); }

NvsReadResult nvs_store_read_blob(const char *namespace_name, const char *key,
                                     size_t maximum_bytes, std::string &value) {
  value.clear();
  if (!g_nvs_ready || namespace_name == nullptr || key == nullptr || maximum_bytes == 0)
    return NvsReadResult::Failed;
  nvs_handle_t handle = 0;
  const std::string stored_key = internal::normalize_nvs_key(key);
  esp_err_t error = nvs_open(namespace_name, NVS_READONLY, &handle);
  if (error != ESP_OK) {
    g_nvs_last_error = esp_err_to_name(error);
    return error == ESP_ERR_NVS_NOT_FOUND ? NvsReadResult::Missing
                                         : NvsReadResult::Failed;
  }
  size_t size = 0;
  error = nvs_get_blob(handle, stored_key.c_str(), nullptr, &size);
  if (error == ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(handle);
    return NvsReadResult::Missing;
  }
  if (error == ESP_OK && (size == 0 || size > maximum_bytes))
    error = ESP_ERR_NVS_INVALID_LENGTH;
  std::string buffer;
  if (error == ESP_OK) {
    buffer.resize(size);
    error = nvs_get_blob(handle, stored_key.c_str(), buffer.data(), &size);
    if (error == ESP_OK) buffer.resize(size);
  }
  nvs_close(handle);
  g_nvs_last_error = error == ESP_OK ? "" : esp_err_to_name(error);
  if (error != ESP_OK) return NvsReadResult::Failed;
  value = std::move(buffer);
  return NvsReadResult::Found;
}

bool nvs_store_write_blob(const char *namespace_name, const char *key,
                          const std::string &value) {
  if (!g_nvs_ready || namespace_name == nullptr || key == nullptr || value.empty())
    return false;
  nvs_handle_t handle = 0;
  const std::string stored_key = internal::normalize_nvs_key(key);
  esp_err_t error = nvs_open(namespace_name, NVS_READWRITE, &handle);
  if (error == ESP_OK) {
    error = nvs_set_blob(handle, stored_key.c_str(), value.data(), value.size());
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
  }
  g_nvs_last_error = error == ESP_OK ? "" : esp_err_to_name(error);
  return error == ESP_OK;
}

bool nvs_store_read_string(const char *namespace_name, const char *key,
                           std::string &value) {
  return nvs_store_read_string_bounded(namespace_name, key, SIZE_MAX, value) ==
         NvsReadResult::Found;
}

NvsReadResult nvs_store_read_string_bounded(const char *namespace_name, const char *key,
                                            size_t maximum_bytes, std::string &value) {
  value.clear();
  if (!g_nvs_ready || namespace_name == nullptr || key == nullptr) {
    return NvsReadResult::Failed;
  }

  const std::string stored_key = internal::normalize_nvs_key(key);
  nvs_handle_t handle = 0;
  esp_err_t error = nvs_open(namespace_name, NVS_READONLY, &handle);
  if (error != ESP_OK) {
    g_nvs_last_error = esp_err_to_name(error);
    return error == ESP_ERR_NVS_NOT_FOUND ? NvsReadResult::Missing : NvsReadResult::Failed;
  }

  size_t required_size = 0;
  error = nvs_get_str(handle, stored_key.c_str(), nullptr, &required_size);
  if (error == ESP_OK && (required_size == 0 || required_size - 1 > maximum_bytes))
    error = ESP_ERR_NVS_INVALID_LENGTH;
  if (error != ESP_OK) {
    nvs_close(handle);
    g_nvs_last_error = esp_err_to_name(error);
    return error == ESP_ERR_NVS_NOT_FOUND ? NvsReadResult::Missing : NvsReadResult::Failed;
  }

  std::string buffer(required_size, '\0');
  error = nvs_get_str(handle, stored_key.c_str(), buffer.data(), &required_size);
  nvs_close(handle);
  if (error != ESP_OK) {
    g_nvs_last_error = esp_err_to_name(error);
    return NvsReadResult::Failed;
  }

  if (required_size == 0 || required_size > buffer.size() || buffer[required_size - 1] != '\0') {
    g_nvs_last_error = esp_err_to_name(ESP_ERR_NVS_INVALID_LENGTH);
    return NvsReadResult::Failed;
  }
  buffer.resize(required_size - 1);
  value = std::move(buffer);
  g_nvs_last_error = "";
  return NvsReadResult::Found;
}

bool nvs_store_write_string(const char *namespace_name, const char *key,
                            const std::string &value) {
  if (!g_nvs_ready || namespace_name == nullptr || key == nullptr) {
    return false;
  }

  const std::string stored_key = internal::normalize_nvs_key(key);
  nvs_handle_t handle = 0;
  esp_err_t error = nvs_open(namespace_name, NVS_READWRITE, &handle);
  if (error != ESP_OK) {
    g_nvs_last_error = esp_err_to_name(error);
    return false;
  }

  error = nvs_set_str(handle, stored_key.c_str(), value.c_str());
  if (error == ESP_OK) {
    error = nvs_commit(handle);
  }
  nvs_close(handle);
  g_nvs_last_error = error == ESP_OK ? "" : esp_err_to_name(error);
  return error == ESP_OK;
}

bool nvs_store_read_bool(const char *namespace_name, const char *key,
                         bool &value) {
  if (!g_nvs_ready || namespace_name == nullptr || key == nullptr) {
    return false;
  }

  const std::string stored_key = internal::normalize_nvs_key(key);
  nvs_handle_t handle = 0;
  esp_err_t error = nvs_open(namespace_name, NVS_READONLY, &handle);
  if (error != ESP_OK) {
    g_nvs_last_error = esp_err_to_name(error);
    return false;
  }

  uint8_t stored = 0;
  error = nvs_get_u8(handle, stored_key.c_str(), &stored);
  nvs_close(handle);
  if (error != ESP_OK) {
    g_nvs_last_error = esp_err_to_name(error);
    return false;
  }
  value = stored != 0;
  g_nvs_last_error = "";
  return true;
}

bool nvs_store_write_bool(const char *namespace_name, const char *key,
                          bool value) {
  if (!g_nvs_ready || namespace_name == nullptr || key == nullptr) {
    return false;
  }

  const std::string stored_key = internal::normalize_nvs_key(key);
  nvs_handle_t handle = 0;
  esp_err_t error = nvs_open(namespace_name, NVS_READWRITE, &handle);
  if (error != ESP_OK) {
    g_nvs_last_error = esp_err_to_name(error);
    return false;
  }

  error = nvs_set_u8(handle, stored_key.c_str(), value ? 1 : 0);
  if (error == ESP_OK) {
    error = nvs_commit(handle);
  }
  nvs_close(handle);
  g_nvs_last_error = error == ESP_OK ? "" : esp_err_to_name(error);
  return error == ESP_OK;
}

bool nvs_store_erase_key(const char *namespace_name, const char *key) {
  if (!g_nvs_ready || namespace_name == nullptr || key == nullptr) {
    return false;
  }

  const std::string stored_key = internal::normalize_nvs_key(key);
  nvs_handle_t handle = 0;
  esp_err_t error = nvs_open(namespace_name, NVS_READWRITE, &handle);
  if (error != ESP_OK) {
    g_nvs_last_error = esp_err_to_name(error);
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
  g_nvs_last_error = error == ESP_OK ? "" : esp_err_to_name(error);
  return error == ESP_OK;
}

} // namespace lunaredu::lunaros
