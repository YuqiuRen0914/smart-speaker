#pragma once
template <typename... Args> inline void resource_test_log(const char *, const char *, Args...) {}
#define ESP_LOGI(...) resource_test_log(__VA_ARGS__)
#define ESP_LOGW(...) resource_test_log(__VA_ARGS__)
#define ESP_LOGE(...) resource_test_log(__VA_ARGS__)
