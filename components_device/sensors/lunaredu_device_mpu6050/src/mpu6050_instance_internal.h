#pragma once
#include "lunaredu_device_mpu6050_imu.h"
#include <array>

namespace lunaredu::device {
Mpu6050ModuleInstance *default_mpu6050_instance();
struct Mpu6050Telemetry {
  Mpu6050Status status;
  std::array<Mpu6050Sample, 16> samples{};
  uint16_t buffered = 0;
  uint16_t count = 0;
};
Mpu6050Telemetry mpu6050_instance_telemetry(Mpu6050ModuleInstance *instance,
                                          uint32_t after_seq, uint16_t limit);
} // namespace lunaredu::device
