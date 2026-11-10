#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_err.h"
using nvs_handle_t = unsigned;
enum nvs_open_mode_t { NVS_READONLY, NVS_READWRITE };
esp_err_t nvs_open(const char *, nvs_open_mode_t, nvs_handle_t *);
void nvs_close(nvs_handle_t);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_get_str(nvs_handle_t, const char *, char *, size_t *);
esp_err_t nvs_commit(nvs_handle_t);
inline esp_err_t nvs_set_str(nvs_handle_t, const char *, const char *) { return ESP_OK; }
inline esp_err_t nvs_get_u8(nvs_handle_t, const char *, uint8_t *) { return ESP_ERR_NVS_NOT_FOUND; }
inline esp_err_t nvs_set_u8(nvs_handle_t, const char *, uint8_t) { return ESP_OK; }
inline esp_err_t nvs_erase_key(nvs_handle_t, const char *) { return ESP_OK; }
