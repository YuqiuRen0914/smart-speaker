#pragma once

#include <esp_err.h>
#include "lunaredu_device_gpio_button_polled_gpio_button.h"

namespace lunaredu::device::gpio_button {

struct TestConfig { int gpio = -1; bool active_high = false; unsigned debounce_ms = 30; };
// 轮询按键并消抖，打印按下、松开事件
// Nonblocking after initialization. Do not run the product driver concurrently.
// Borrowed BSP/bus pointers must outlive stop_test(). Config itself is copied.
esp_err_t start_test(const TestConfig &config);
// Waits for in-flight test I/O, stops outputs and releases owned resources.
esp_err_t stop_test();

// Pin/bus convenience entry: no application polling or task setup required.
inline esp_err_t start_test(int gpio) {
  TestConfig config;
  config.gpio = gpio;
  return start_test(config);
}

}  // namespace lunaredu::device::gpio_button
