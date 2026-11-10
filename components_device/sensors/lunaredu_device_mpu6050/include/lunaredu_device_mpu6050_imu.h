#pragma once

#include <stdint.h>
#include <string>

namespace lunaredu::lunaros { class DeviceProjectionRegistry; }

#include <driver/i2c_master.h>

namespace lunaredu::device {

struct Mpu6050ModuleConfig {
  uint8_t sda_pin = 0;
  uint8_t scl_pin = 0;
  uint32_t i2c_clock_hz = 400000;
  bool use_bsp_i2c = true;
  int i2c_port = 0;
  uint8_t preferred_address = 0x68;
  void *external_i2c_bus = nullptr;
  bool allow_secondary_address_fallback = true;
  bool has_interrupt_pin = false;
  uint8_t interrupt_pin = 0;
  uint16_t default_sample_rate_hz = 50;
  bool enable_pose = false;
  bool calibrate_gyro_on_setup = false;
  uint16_t gyro_calibration_samples = 3000;
  float pose_accel_coefficient = 0.02f;
  float pose_gyro_coefficient = 0.98f;
};

struct Mpu6050Sample {
  bool valid = false;
  uint32_t seq = 0;
  uint32_t t_ms = 0;
  int16_t accel_raw_x = 0;
  int16_t accel_raw_y = 0;
  int16_t accel_raw_z = 0;
  float accel_g_x = 0;
  float accel_g_y = 0;
  float accel_g_z = 0;
  int16_t gyro_raw_x = 0;
  int16_t gyro_raw_y = 0;
  int16_t gyro_raw_z = 0;
  float gyro_dps_x = 0;
  float gyro_dps_y = 0;
  float gyro_dps_z = 0;
  float temperature_c = 0;
};

struct Mpu6050Pose {
  bool valid = false;
  bool enabled = false;
  bool gyro_calibrated = false;
  uint32_t seq = 0;
  uint32_t t_ms = 0;
  float dt_s = 0;
  float angle_x = 0;
  float angle_y = 0;
  float angle_z = 0;
  float accel_angle_x = 0;
  float accel_angle_y = 0;
  float gyro_angle_x = 0;
  float gyro_angle_y = 0;
  float gyro_angle_z = 0;
  float gyro_x_dps = 0;
  float gyro_y_dps = 0;
  float gyro_z_dps = 0;
  float gyro_offset_x = 0;
  float gyro_offset_y = 0;
  float gyro_offset_z = 0;
};

struct Mpu6050Status {
  bool ok = false;
  bool initialized = false;
  bool sampling = false;
  bool sleeping = false;
  uint8_t i2c_address = 0;
  uint8_t who_am_i = 0;
  uint16_t sample_rate_hz = 50;
  uint16_t sample_interval_ms = 20;
  uint32_t sample_count = 0;
  uint32_t last_sample_ms = 0;
  std::string last_error;
  Mpu6050Sample latest;
  Mpu6050Pose pose;
};
struct Mpu6050ModuleInstance;
struct Mpu6050DiagnosticFacts {
  bool initialized = false, bus_pins_known = false, has_interrupt_pin = false;
  uint8_t sda_pin = 0, scl_pin = 0, interrupt_pin = 0, address = 0;
  uint32_t clock_hz = 0;
  uint16_t sample_rate_hz = 0;
};
// Configuration-only observation; never reads the I2C device or starts sampling.
Mpu6050DiagnosticFacts mpu6050_instance_diagnostic_facts(Mpu6050ModuleInstance *);

Mpu6050ModuleInstance *mpu6050_create_instance();
// A failed release retains the instance and its owned resources for retry.
bool mpu6050_destroy_instance(Mpu6050ModuleInstance *instance);
bool setup_mpu6050_instance(Mpu6050ModuleInstance *instance,
                            const Mpu6050ModuleConfig &config);
bool mpu6050_instance_deinit(Mpu6050ModuleInstance *instance);
void tick_mpu6050_instance(Mpu6050ModuleInstance *instance);
bool mpu6050_instance_read_now(Mpu6050ModuleInstance *instance);
bool mpu6050_instance_available(Mpu6050ModuleInstance *instance);
Mpu6050Status mpu6050_instance_status(Mpu6050ModuleInstance *instance);
Mpu6050Pose mpu6050_instance_pose(Mpu6050ModuleInstance *instance);
bool mpu6050_instance_calibrate_gyro(Mpu6050ModuleInstance *instance,
                                     uint16_t samples, std::string &error);
void mpu6050_instance_reset_pose(Mpu6050ModuleInstance *instance);
bool mpu6050_instance_start_sampling(Mpu6050ModuleInstance *instance,
                                     std::string &error);
void mpu6050_instance_stop_sampling(Mpu6050ModuleInstance *instance);
// Suspending invalidates old samples and pose; waking does not start polling.
bool mpu6050_instance_set_sleep(Mpu6050ModuleInstance *instance, bool sleep,
                                std::string &error);
bool mpu6050_instance_set_sample_rate_hz(Mpu6050ModuleInstance *instance,
                                         uint16_t sample_rate_hz,
                                         std::string &error);

bool setup_mpu6050(const Mpu6050ModuleConfig &config);
bool setup_mpu6050_with_bus(const Mpu6050ModuleConfig &config,
                            i2c_master_bus_handle_t bus_handle);
bool mpu6050_deinit();
bool setup_main_imu_module();
void tick_mpu6050();
// For an I/O owner that already supplies its own sampling deadline.
bool mpu6050_read_now();
bool mpu6050_available();
Mpu6050Status mpu6050_status();
Mpu6050Pose mpu6050_pose();
bool mpu6050_calibrate_gyro(uint16_t samples, std::string &error);
void mpu6050_reset_pose();
bool mpu6050_start_sampling(std::string &error);
void mpu6050_stop_sampling();
bool mpu6050_set_sleep(bool sleep, std::string &error);
bool mpu6050_set_sample_rate_hz(uint16_t sample_rate_hz, std::string &error);
bool declare_mpu6050_projection(lunaredu::lunaros::DeviceProjectionRegistry &registry,
                                std::string &error);

} // namespace lunaredu::device
