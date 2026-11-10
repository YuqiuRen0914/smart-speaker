#include <stddef.h>
#include "espnow_storage.h"
// Application security/NVS provisioning is outside this data transport. Never
// permit the upstream convenience helper to erase the shared NVS partition.
esp_err_t espnow_storage_init(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t espnow_storage_set(const char *key, const void *value, size_t length) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t espnow_storage_get(const char *key, void *value, size_t length) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t espnow_storage_erase(const char *key) { return ESP_ERR_NOT_SUPPORTED; }
