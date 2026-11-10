#pragma once
#include "freertos/FreeRTOS.h"
inline int64_t esp_timer_get_time() { return runtime_test::time_us; }
