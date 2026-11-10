#pragma once
using esp_err_t = int;
constexpr int ESP_OK = 0;
constexpr int ESP_FAIL = -1;
constexpr int ESP_ERR_NVS_NOT_FOUND = 1;
constexpr int ESP_ERR_NVS_INVALID_LENGTH = 2;
constexpr int ESP_ERR_NVS_NO_FREE_PAGES = 3;
constexpr int ESP_ERR_NVS_NEW_VERSION_FOUND = 4;
inline const char *esp_err_to_name(int error) { return error == 0 ? "ESP_OK" : "error"; }
#define ESP_ERROR_CHECK_WITHOUT_ABORT(value) ((void)(value))
