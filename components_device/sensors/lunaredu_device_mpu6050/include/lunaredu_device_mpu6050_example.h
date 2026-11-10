#pragma once
#include <esp_err.h>

namespace lunaredu::device::mpu6050 {
// Blocking minimum test: default parameters, about ten seconds, then cleanup.
// Call from an ESP-IDF task with exclusive access to this peripheral.
esp_err_t example(int sda_gpio, int scl_gpio);
} // namespace lunaredu::device::mpu6050
