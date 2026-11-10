#pragma once
using esp_err_t = int;
inline constexpr int ESP_OK = 0;
inline constexpr int ESP_FAIL = -1;
inline const char *esp_err_to_name(int) { return "ESP_FAIL"; }
