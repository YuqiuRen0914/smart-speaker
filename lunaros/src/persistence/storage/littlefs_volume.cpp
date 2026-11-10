#include "persistence/storage/littlefs_volume.h"

#include <mutex>
#include <string>

#include <esp_err.h>
#include <esp_littlefs.h>

#include "lunaredu_lunaros/kernel/log.h"
#include "littlefs_store_internal.h"

namespace {

constexpr const char *kTag = "system.littlefs";
LittlefsStoreState &state = littlefs_store_state();

} // namespace

bool setup_littlefs_store() {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  if (state.ready) {
    return true;
  }

  esp_vfs_littlefs_conf_t config = {};
  config.base_path = kLittlefsMountPath;
  config.partition_label = kLittlefsPartitionLabel;
  config.format_if_mount_failed = false;
  config.read_only = false;
  config.dont_mount = false;
  config.grow_on_mount = true;

  const esp_err_t result = esp_vfs_littlefs_register(&config);
  state.ready = result == ESP_OK || result == ESP_ERR_INVALID_STATE;
  state.last_error = state.ready ? "" : esp_err_to_name(result);

  if (state.ready) {
    if (!littlefs_internal_cleanup_writes()) {
      state.ready = false;
      return false;
    }
    littlefs_internal_refresh_usage();
    LOGI(kTag, "LittleFS 已就绪：分区=%s 总容量=%u 已用=%u",
         kLittlefsPartitionLabel, static_cast<unsigned>(state.total_bytes),
         static_cast<unsigned>(state.used_bytes));
  } else {
    LOGE(kTag, "挂载 LittleFS 失败：%s", state.last_error.c_str());
  }
  return state.ready;
}

bool littlefs_store_ready() {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  return state.ready;
}

const char *littlefs_store_mount_path() { return kLittlefsMountPath; }

const char *littlefs_store_partition_label() { return kLittlefsPartitionLabel; }

size_t littlefs_store_total_bytes() {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  return state.total_bytes;
}

size_t littlefs_store_used_bytes() {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  return state.used_bytes;
}

bool littlefs_store_refresh_usage() {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  return littlefs_internal_refresh_usage();
}

bool littlefs_store_format() {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  for (const auto &stream : state.streams) {
    if (stream.path != nullptr) {
      return littlefs_internal_fail(nullptr, "storage_stream_active");
    }
  }
  if (state.ready) {
    const esp_err_t unregister_result =
        esp_vfs_littlefs_unregister(kLittlefsPartitionLabel);
    if (unregister_result != ESP_OK &&
        unregister_result != ESP_ERR_INVALID_STATE) {
      state.last_error = esp_err_to_name(unregister_result);
      return false;
    }
    state.ready = false;
    state.total_bytes = 0;
    state.used_bytes = 0;
  }

  const esp_err_t format_result = esp_littlefs_format(kLittlefsPartitionLabel);
  if (format_result != ESP_OK) {
    state.last_error = esp_err_to_name(format_result);
    LOGE(kTag, "显式格式化 LittleFS 失败：%s", state.last_error.c_str());
    return false;
  }

  LOGW(kTag, "LittleFS 分区已被显式格式化");
  return setup_littlefs_store();
}

const char *littlefs_store_last_error() {
  thread_local std::string last_error;
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  last_error = state.last_error;
  return last_error.c_str();
}
