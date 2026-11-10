#pragma once
#include "esp_err.h"
struct esp_vfs_littlefs_conf_t {
  const char *base_path = nullptr;
  const char *partition_label = nullptr;
  bool format_if_mount_failed = false, read_only = false, dont_mount = false;
  bool grow_on_mount = false;
};
inline int esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *) { return ESP_OK; }
inline int esp_vfs_littlefs_unregister(const char *) { return ESP_OK; }
inline int esp_littlefs_format(const char *) { return ESP_OK; }
