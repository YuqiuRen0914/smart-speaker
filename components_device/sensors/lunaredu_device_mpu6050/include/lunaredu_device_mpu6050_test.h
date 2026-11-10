#pragma once

#include <esp_err.h>
#include "lunaredu_device_mpu6050_imu.h"

namespace lunaredu::device::mpu6050 {

using TestConfig = Mpu6050ModuleConfig;

// 独立实例读取六轴数据
// Nonblocking after initialization. Do not run the product driver concurrently.
// Borrowed BSP/bus pointers must outlive stop_test(). Config itself is copied.
esp_err_t start_test(const TestConfig &config);
// Waits for in-flight test I/O, stops outputs and releases owned resources.
esp_err_t stop_test();

// Pin/bus convenience entry: no application polling or task setup required.
inline esp_err_t start_test(int sda_gpio, int scl_gpio) {
  if (sda_gpio < 0 || sda_gpio > 255 || scl_gpio < 0 || scl_gpio > 255) return ESP_ERR_INVALID_ARG;
  TestConfig config; config.sda_pin = static_cast<uint8_t>(sda_gpio); config.scl_pin = static_cast<uint8_t>(scl_gpio); config.use_bsp_i2c = false;
  return start_test(config);
}

}  // namespace lunaredu::device::mpu6050
