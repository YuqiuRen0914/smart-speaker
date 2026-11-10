#pragma once
#include "esp_err.h"
inline esp_err_t test_nvs_init_error = ESP_OK;
inline int test_nvs_erases = 0;
inline esp_err_t nvs_flash_init() { return test_nvs_init_error; }
inline esp_err_t nvs_flash_erase() {
  ++test_nvs_erases;
  return ESP_OK;
}
