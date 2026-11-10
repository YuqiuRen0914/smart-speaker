#pragma once
#include <cstdio>
#define ESP_LOGI(tag, fmt, ...) do { if (false) std::printf(fmt, ##__VA_ARGS__); (void)(tag); } while (0)
#define ESP_LOGW ESP_LOGI
#define ESP_LOGE ESP_LOGI
#define ESP_LOGD ESP_LOGI
