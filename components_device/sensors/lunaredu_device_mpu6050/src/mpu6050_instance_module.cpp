#include "mpu6050_instance_internal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <string>

#include <driver/i2c_master.h>
#include <esp_err.h>
#include <esp_timer.h>
#ifdef ESP_PLATFORM
#include <esp_attr.h>
#endif

#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "mpu6050.h"

namespace lunaredu::device {

struct Mpu6050ModuleInstance {
  bool initialized = false;
  bool sampling = false;
  bool sleeping = false;
  bool owns_i2c_bus = false;
  Mpu6050ModuleConfig config;
  uint8_t i2c_address = 0;
  uint8_t who_am_i = 0;
  uint16_t sample_rate_hz = 50;
  uint16_t sample_interval_ms = 20;
  uint32_t sample_count = 0;
  uint32_t last_sample_ms = 0;
  uint32_t last_tick_ms = 0;
  std::string last_error;
  Mpu6050Sample latest_sample;
  Mpu6050Sample sample_ring[64];
  uint16_t sample_ring_write_index = 0;
  Mpu6050Pose pose;
  uint32_t pose_last_ms = 0;
  i2c_bus_handle_t custom_i2c_bus = nullptr;
  i2c_master_bus_handle_t i2c_master_bus = nullptr;
  mpu6050_handle_t sensor = nullptr;
  i2c_master_dev_handle_t probe = nullptr;
  std::recursive_mutex mutex;
};

namespace {

constexpr const char *kLogTag = "mpu6050_inst";
constexpr uint8_t kMpu6050AddressPrimary = 0x68;
constexpr uint8_t kMpu6050AddressSecondary = 0x69;
constexpr uint8_t kRegisterWhoAmI = 0x75;
constexpr uint8_t kSupportedWhoAmI[] = {
    0x68,  // MPU-6050
    0x70,  // MPU-6500-compatible
    0x71,  // MPU-9250-compatible
    0x73,  // MPU-9255-compatible
    0x74,  // MPU-9515-compatible / common MPU clone module ID
};
constexpr int kI2cTransferTimeoutMs = 100;
constexpr uint16_t kDefaultSampleRateHz = 50;
constexpr uint16_t kMinSampleRateHz = 5;
constexpr uint16_t kMaxSampleRateHz = 1000;
constexpr uint16_t kTelemetryBufferSize = 64;
constexpr float kAccelLsbPerG = 16384.0f;
constexpr float kGyroLsbPerDps = 131.0f;
constexpr float kRadToDeg = 57.29577951308232f;
constexpr uint16_t kDefaultGyroCalibrationSamples = 3000;

uint16_t sanitize_sample_rate_hz(uint16_t sample_rate_hz) {
  if (sample_rate_hz < kMinSampleRateHz || sample_rate_hz > kMaxSampleRateHz) {
    return kDefaultSampleRateHz;
  }
  return sample_rate_hz;
}

float clamp_pose_coefficient(float value, float fallback) {
  if (!std::isfinite(value) || value < 0.0f || value > 1.0f) {
    return fallback;
  }
  return value;
}

int16_t scaled_raw(float value, float lsb_per_unit) {
  const long raw = std::lround(value * lsb_per_unit);
  if (raw > std::numeric_limits<int16_t>::max()) {
    return std::numeric_limits<int16_t>::max();
  }
  if (raw < std::numeric_limits<int16_t>::min()) {
    return std::numeric_limits<int16_t>::min();
  }
  return static_cast<int16_t>(raw);
}

bool release_sensor_handle(Mpu6050ModuleInstance &instance) {
  if (instance.sensor != nullptr) {
    if (mpu6050_delete(instance.sensor) != ESP_OK) {
      instance.last_error = "sensor_delete_failed";
      return false;
    }
    instance.sensor = nullptr;
  }
  return true;
}

bool is_supported_who_am_i(uint8_t value) {
  for (const uint8_t supported : kSupportedWhoAmI) {
    if (value == supported) return true;
  }
  return false;
}

bool release_i2c_transport(Mpu6050ModuleInstance &instance) {
  if (instance.owns_i2c_bus && instance.custom_i2c_bus != nullptr) {
    const uint8_t device_count =
        i2c_bus_get_created_device_num(instance.custom_i2c_bus);
    if (device_count == 0) {
      i2c_bus_handle_t bus = instance.custom_i2c_bus;
      const esp_err_t err = i2c_bus_delete(&bus);
      if (err != ESP_OK) {
        instance.last_error = "i2c_bus_delete_failed";
        return false;
      }
    } else {
      instance.last_error = "i2c_bus_busy";
      return false;
    }
  }
  instance.custom_i2c_bus = nullptr;
  instance.i2c_master_bus = nullptr;
  instance.owns_i2c_bus = false;
  return true;
}

bool release_resources(Mpu6050ModuleInstance &instance) {
  instance.sampling = false;
  instance.initialized = false;
  if (!release_sensor_handle(instance)) return false;
  if (instance.probe) {
    if (i2c_master_bus_rm_device(instance.probe) != ESP_OK) {
      instance.last_error = "probe_delete_failed";
      return false;
    }
    instance.probe = nullptr;
  }
  return release_i2c_transport(instance);
}

void reset_runtime_state(Mpu6050ModuleInstance &instance,
                         const Mpu6050ModuleConfig &config) {
  instance.config = config;
  instance.initialized = false;
  instance.sampling = false;
  instance.i2c_address = 0;
  instance.sleeping = false;
  instance.who_am_i = 0;
  instance.sample_rate_hz =
      sanitize_sample_rate_hz(config.default_sample_rate_hz);
  instance.sample_interval_ms =
      static_cast<uint16_t>(1000 / instance.sample_rate_hz);
  instance.sample_count = 0;
  instance.last_sample_ms = 0;
  instance.last_tick_ms = 0;
  instance.last_error.clear();
  instance.latest_sample = Mpu6050Sample();
  for (uint16_t index = 0; index < kTelemetryBufferSize; ++index) {
    instance.sample_ring[index] = Mpu6050Sample();
  }
  instance.sample_ring_write_index = 0;
  instance.pose = Mpu6050Pose();
  instance.pose.enabled = config.enable_pose;
  instance.pose_last_ms = 0;
}

bool fail_setup(Mpu6050ModuleInstance &instance) {
  const std::string last_error = instance.last_error;
  release_resources(instance);
  instance.initialized = false;
  instance.last_error = last_error;
  return false;
}

bool setup_custom_i2c_transport(Mpu6050ModuleInstance &instance) {
  if (instance.config.external_i2c_bus != nullptr) {
    instance.i2c_master_bus =
        static_cast<i2c_master_bus_handle_t>(instance.config.external_i2c_bus);
    instance.owns_i2c_bus = false;
    if (instance.i2c_master_bus == nullptr) {
      instance.last_error = "external_i2c_bus_unavailable";
      return false;
    }
    return true;
  }

  if (instance.config.i2c_port < 0) {
    instance.last_error = "invalid_i2c_port";
    return false;
  }

  i2c_config_t config = {};
  config.mode = I2C_MODE_MASTER;
  config.sda_io_num = instance.config.sda_pin;
  config.sda_pullup_en = GPIO_PULLUP_ENABLE;
  config.scl_io_num = instance.config.scl_pin;
  config.scl_pullup_en = GPIO_PULLUP_ENABLE;
  config.master.clk_speed = instance.config.i2c_clock_hz;
  config.clk_flags = 0;

  instance.custom_i2c_bus =
      i2c_bus_create(static_cast<i2c_port_t>(instance.config.i2c_port), &config);
  if (instance.custom_i2c_bus == nullptr) {
    instance.last_error = "i2c_bus_create_failed";
    return false;
  }
  instance.owns_i2c_bus = true;

  instance.i2c_master_bus =
      i2c_bus_get_internal_bus_handle(instance.custom_i2c_bus);
  if (instance.i2c_master_bus == nullptr) {
    instance.last_error = "i2c_master_bus_unavailable";
    return false;
  }

  return true;
}

mpu6050_config_t make_driver_config(Mpu6050ModuleInstance &instance,
                                    uint8_t address) {
  mpu6050_config_t config = {};
  config.i2c_address = address;
  config.i2c_clock_speed = instance.config.i2c_clock_hz;
  config.low_pass_filter = MPU6050_DIGITAL_LP_FILTER_ACCEL_44KHZ_GYRO_42KHZ;
  config.gyro_clock_source = MPU6050_GYRO_CS_PLL_X_AXIS_REF;
  config.gyro_full_scale_range = MPU6050_GYRO_FS_RANGE_250DPS;
  config.accel_full_scale_range = MPU6050_ACCEL_FS_RANGE_2G;
  config.irq_io_num =
      instance.config.has_interrupt_pin
          ? static_cast<gpio_num_t>(instance.config.interrupt_pin)
          : GPIO_NUM_NC;
  config.irq_io_active_level = MPU6050_IRQ_PIN_ACTIVE_HIGH;
  config.irq_io_mode = MPU6050_IRQ_PIN_PUSH_PULL;
  config.irq_latch = MPU6050_IRQ_LATCH_50US;
  config.irq_clear_behavior = MPU6050_IRQ_CLEAR_ON_STATUS_READ;
  return config;
}

bool probe_sensor_identity(Mpu6050ModuleInstance &instance, uint8_t address) {
  if (instance.probe) {
    if (i2c_master_bus_rm_device(instance.probe) != ESP_OK) return false;
    instance.probe = nullptr;
  }
  i2c_device_config_t device_config = {};
  device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device_config.device_address = address;
  device_config.scl_speed_hz = instance.config.i2c_clock_hz;

  esp_err_t result = i2c_master_bus_add_device(instance.i2c_master_bus,
                                               &device_config, &instance.probe);
  if (result != ESP_OK || instance.probe == nullptr) {
    instance.last_error = "probe_device_create_failed";
    return false;
  }

  uint8_t who_am_i = 0;
  result = i2c_master_transmit_receive(instance.probe, &kRegisterWhoAmI, 1,
                                       &who_am_i, 1, kI2cTransferTimeoutMs);
  if (i2c_master_bus_rm_device(instance.probe) != ESP_OK) {
    instance.last_error = "probe_delete_failed";
    return false;
  }
  instance.probe = nullptr;

  if (result != ESP_OK) {
    instance.last_error = "device_not_found";
    return false;
  }

  if (!is_supported_who_am_i(who_am_i)) {
    instance.last_error = "unexpected_who_am_i";
    return false;
  }

  instance.who_am_i = who_am_i;
  return true;
}

bool create_sensor_at_address(Mpu6050ModuleInstance &instance,
                              uint8_t address) {
  if (instance.i2c_master_bus == nullptr) {
    instance.last_error = "i2c_master_bus_unavailable";
    return false;
  }

  if (!release_sensor_handle(instance)) return false;
  if (!probe_sensor_identity(instance, address)) {
    return false;
  }

  const mpu6050_config_t driver_config = make_driver_config(instance, address);
  const esp_err_t init_result =
      mpu6050_init(instance.i2c_master_bus, &driver_config, &instance.sensor);
  if (init_result != ESP_OK || instance.sensor == nullptr) {
    instance.last_error = "driver_init_failed";
    return false;
  }

  mpu6050_who_am_i_register_t who_am_i = {};
  if (mpu6050_get_who_am_i_register(instance.sensor, &who_am_i) != ESP_OK) {
    instance.last_error = "who_am_i_read_failed";
    release_sensor_handle(instance);
    return false;
  }

  if (!is_supported_who_am_i(who_am_i.reg)) {
    instance.last_error = "unexpected_who_am_i";
    release_sensor_handle(instance);
    return false;
  }

  instance.i2c_address = address;
  instance.who_am_i = who_am_i.reg;
  return true;
}

bool detect_device(Mpu6050ModuleInstance &instance) {
  if (instance.config.preferred_address == kMpu6050AddressPrimary ||
      instance.config.preferred_address == kMpu6050AddressSecondary) {
    if (create_sensor_at_address(instance, instance.config.preferred_address)) {
      return true;
    }

    if (!instance.config.allow_secondary_address_fallback) {
      return false;
    }

    const uint8_t fallback_address =
        instance.config.preferred_address == kMpu6050AddressPrimary
            ? kMpu6050AddressSecondary
            : kMpu6050AddressPrimary;
    return create_sensor_at_address(instance, fallback_address);
  }

  if (create_sensor_at_address(instance, kMpu6050AddressPrimary)) {
    return true;
  }

  if (create_sensor_at_address(instance, kMpu6050AddressSecondary)) {
    return true;
  }

  instance.last_error = "device_not_found";
  return false;
}

bool apply_sample_rate_register(Mpu6050ModuleInstance &instance,
                                uint16_t sample_rate_hz) {
  if (instance.sensor == nullptr) {
    return false;
  }

  const uint16_t clamped =
      sample_rate_hz < kMinSampleRateHz ? kMinSampleRateHz : sample_rate_hz;
  const uint16_t limited =
      clamped > kMaxSampleRateHz ? kMaxSampleRateHz : clamped;
  const uint16_t divider = (1000 / limited) - 1;
  return mpu6050_set_sample_rate_divider_register(
             instance.sensor, static_cast<uint8_t>(divider & 0xFF)) == ESP_OK;
}

bool configure_device(Mpu6050ModuleInstance &instance) {
  if (instance.sensor == nullptr) {
    instance.last_error = "driver_not_ready";
    return false;
  }

  mpu6050_config_register_t config = {};
  config.bits.low_pass_filter = MPU6050_DIGITAL_LP_FILTER_ACCEL_44KHZ_GYRO_42KHZ;
  config.bits.ext_sync_setting = MPU6050_EXT_SYNC_SETTING_INPUT_DISABLED;
  if (mpu6050_set_config_register(instance.sensor, config) != ESP_OK) {
    instance.last_error = "config_failed";
    return false;
  }

  mpu6050_gyro_config_register_t gyro_config = {};
  gyro_config.bits.full_scale_range = MPU6050_GYRO_FS_RANGE_250DPS;
  if (mpu6050_set_gyro_config_register(instance.sensor, gyro_config) != ESP_OK) {
    instance.last_error = "gyro_config_failed";
    return false;
  }

  mpu6050_accel_config_register_t accel_config = {};
  accel_config.bits.full_scale_range = MPU6050_ACCEL_FS_RANGE_2G;
  if (mpu6050_set_accel_config_register(instance.sensor, accel_config) !=
      ESP_OK) {
    instance.last_error = "accel_config_failed";
    return false;
  }

  mpu6050_power_management1_register_t power = {};
  power.bits.clock_source = MPU6050_GYRO_CS_PLL_X_AXIS_REF;
  power.bits.sleep_enabled = false;
  power.bits.cycle_enabled = false;
  power.bits.temp_disabled = false;
  power.bits.reset_enabled = false;
  if (mpu6050_set_power_management1_register(instance.sensor, power) !=
      ESP_OK) {
    instance.last_error = "wake_failed";
    return false;
  }

  if (!apply_sample_rate_register(instance, instance.sample_rate_hz)) {
    instance.last_error = "sample_rate_config_failed";
    return false;
  }
  return true;
}

void reset_pose_state(Mpu6050ModuleInstance &instance) {
  const bool enabled = instance.pose.enabled;
  const bool calibrated = instance.pose.gyro_calibrated;
  const float offset_x = instance.pose.gyro_offset_x;
  const float offset_y = instance.pose.gyro_offset_y;
  const float offset_z = instance.pose.gyro_offset_z;

  instance.pose = Mpu6050Pose();
  instance.pose.enabled = enabled;
  instance.pose.gyro_calibrated = calibrated;
  instance.pose.gyro_offset_x = offset_x;
  instance.pose.gyro_offset_y = offset_y;
  instance.pose.gyro_offset_z = offset_z;
  instance.pose_last_ms = 0;
}

void update_pose_from_sample(Mpu6050ModuleInstance &instance,
                             const Mpu6050Sample &sample) {
  if (!instance.pose.enabled || !sample.valid) {
    return;
  }

  const float accel_angle_x =
      std::atan2(sample.accel_g_y,
                 sample.accel_g_z + std::fabs(sample.accel_g_x)) *
      kRadToDeg;
  const float accel_angle_y =
      std::atan2(sample.accel_g_x,
                 sample.accel_g_z + std::fabs(sample.accel_g_y)) *
      -kRadToDeg;

  const float gyro_x = sample.gyro_dps_x - instance.pose.gyro_offset_x;
  const float gyro_y = sample.gyro_dps_y - instance.pose.gyro_offset_y;
  const float gyro_z = sample.gyro_dps_z - instance.pose.gyro_offset_z;

  float dt_s = 0.0f;
  if (instance.pose_last_ms != 0 && sample.t_ms >= instance.pose_last_ms) {
    dt_s = static_cast<float>(sample.t_ms - instance.pose_last_ms) * 0.001f;
  }
  if (dt_s <= 0.0f || dt_s > 0.5f) {
    dt_s = static_cast<float>(instance.sample_interval_ms) * 0.001f;
  }

  if (!instance.pose.valid) {
    instance.pose.angle_x = accel_angle_x;
    instance.pose.angle_y = accel_angle_y;
    instance.pose.angle_z = 0.0f;
    instance.pose.gyro_angle_x = 0.0f;
    instance.pose.gyro_angle_y = 0.0f;
    instance.pose.gyro_angle_z = 0.0f;
    dt_s = 0.0f;
  } else {
    instance.pose.gyro_angle_x += gyro_x * dt_s;
    instance.pose.gyro_angle_y += gyro_y * dt_s;
    instance.pose.gyro_angle_z += gyro_z * dt_s;

    const float acc_coef =
        clamp_pose_coefficient(instance.config.pose_accel_coefficient, 0.02f);
    const float gyro_coef =
        clamp_pose_coefficient(instance.config.pose_gyro_coefficient, 0.98f);
    const float coef_sum = acc_coef + gyro_coef;
    const float normalized_acc_coef =
        coef_sum <= 0.0f ? 0.02f : acc_coef / coef_sum;
    const float normalized_gyro_coef =
        coef_sum <= 0.0f ? 0.98f : gyro_coef / coef_sum;

    instance.pose.angle_x =
        normalized_gyro_coef * (instance.pose.angle_x + gyro_x * dt_s) +
        normalized_acc_coef * accel_angle_x;
    instance.pose.angle_y =
        normalized_gyro_coef * (instance.pose.angle_y + gyro_y * dt_s) +
        normalized_acc_coef * accel_angle_y;
    instance.pose.angle_z = instance.pose.gyro_angle_z;
  }

  instance.pose.valid = true;
  instance.pose.seq = sample.seq;
  instance.pose.t_ms = sample.t_ms;
  instance.pose.dt_s = dt_s;
  instance.pose.accel_angle_x = accel_angle_x;
  instance.pose.accel_angle_y = accel_angle_y;
  instance.pose.gyro_x_dps = gyro_x;
  instance.pose.gyro_y_dps = gyro_y;
  instance.pose.gyro_z_dps = gyro_z;
  instance.pose_last_ms = sample.t_ms;
}

bool read_gyro_dps(Mpu6050ModuleInstance &instance, float &x, float &y,
                   float &z) {
  if (instance.sensor == nullptr) {
    instance.last_error = "driver_not_ready";
    return false;
  }

  mpu6050_gyro_data_axes_t gyro = {};
  mpu6050_accel_data_axes_t accel = {};
  float temperature = 0.0f;
  if (mpu6050_get_motion(instance.sensor, &gyro, &accel, &temperature) !=
      ESP_OK) {
    instance.last_error = "gyro_read_failed";
    return false;
  }

  x = gyro.x_axis;
  y = gyro.y_axis;
  z = gyro.z_axis;
  return true;
}

bool calibrate_gyro_offsets(Mpu6050ModuleInstance &instance, uint16_t samples,
                            std::string &error) {
  const uint16_t sample_count =
      samples == 0 ? kDefaultGyroCalibrationSamples : samples;
  if (sample_count == 0) {
    error = "invalid_calibration_samples";
    return false;
  }

  float sum_x = 0.0f;
  float sum_y = 0.0f;
  float sum_z = 0.0f;
  for (uint16_t index = 0; index < sample_count; ++index) {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    if (!read_gyro_dps(instance, x, y, z)) {
      error =
          instance.last_error.empty() ? "gyro_read_failed" : instance.last_error;
      return false;
    }

    sum_x += x;
    sum_y += y;
    sum_z += z;
    if ((index % 64) == 63) {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }

  instance.pose.gyro_offset_x = sum_x / static_cast<float>(sample_count);
  instance.pose.gyro_offset_y = sum_y / static_cast<float>(sample_count);
  instance.pose.gyro_offset_z = sum_z / static_cast<float>(sample_count);
  instance.pose.gyro_calibrated = true;
  reset_pose_state(instance);
  error.clear();
  return true;
}

bool read_sample(Mpu6050ModuleInstance &instance) {
  if (instance.sensor == nullptr) {
    instance.last_error = "driver_not_ready";
    return false;
  }

  mpu6050_gyro_data_axes_t gyro = {};
  mpu6050_accel_data_axes_t accel = {};
  float temperature = 0.0f;
  if (mpu6050_get_motion(instance.sensor, &gyro, &accel, &temperature) !=
      ESP_OK) {
    instance.last_error = "sample_read_failed";
    return false;
  }

  instance.latest_sample.valid = true;
  instance.latest_sample.seq = ++instance.sample_count;
  instance.latest_sample.t_ms =
      static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  instance.latest_sample.accel_g_x = accel.x_axis;
  instance.latest_sample.accel_g_y = accel.y_axis;
  instance.latest_sample.accel_g_z = accel.z_axis;
  instance.latest_sample.gyro_dps_x = gyro.x_axis;
  instance.latest_sample.gyro_dps_y = gyro.y_axis;
  instance.latest_sample.gyro_dps_z = gyro.z_axis;
  instance.latest_sample.temperature_c = temperature;
  instance.latest_sample.accel_raw_x =
      scaled_raw(instance.latest_sample.accel_g_x, kAccelLsbPerG);
  instance.latest_sample.accel_raw_y =
      scaled_raw(instance.latest_sample.accel_g_y, kAccelLsbPerG);
  instance.latest_sample.accel_raw_z =
      scaled_raw(instance.latest_sample.accel_g_z, kAccelLsbPerG);
  instance.latest_sample.gyro_raw_x =
      scaled_raw(instance.latest_sample.gyro_dps_x, kGyroLsbPerDps);
  instance.latest_sample.gyro_raw_y =
      scaled_raw(instance.latest_sample.gyro_dps_y, kGyroLsbPerDps);
  instance.latest_sample.gyro_raw_z =
      scaled_raw(instance.latest_sample.gyro_dps_z, kGyroLsbPerDps);

  update_pose_from_sample(instance, instance.latest_sample);
  instance.sample_ring[instance.sample_ring_write_index] =
      instance.latest_sample;
  instance.sample_ring_write_index = static_cast<uint16_t>(
      (instance.sample_ring_write_index + 1) % kTelemetryBufferSize);
  instance.last_sample_ms = instance.latest_sample.t_ms;
  instance.last_error.clear();
  return true;
}

Mpu6050Status make_status_locked(const Mpu6050ModuleInstance &instance) {
  Mpu6050Status status;
  status.ok = instance.initialized;
  status.initialized = instance.initialized;
  status.sampling = instance.sampling;
  status.i2c_address = instance.i2c_address;
  status.who_am_i = instance.who_am_i;
  status.sample_rate_hz = instance.sample_rate_hz;
  status.sleeping = instance.sleeping;
  status.sample_interval_ms = instance.sample_interval_ms;
  status.sample_count = instance.sample_count;
  status.last_sample_ms = instance.last_sample_ms;
  status.last_error = instance.last_error;
  status.latest = instance.latest_sample;
  status.pose = instance.pose;
  return status;
}

} // namespace

Mpu6050ModuleInstance *mpu6050_create_instance() {
  return new Mpu6050ModuleInstance();
}

bool mpu6050_destroy_instance(Mpu6050ModuleInstance *instance) {
  if (instance == nullptr) {
    return true;
  }
  if (!mpu6050_instance_deinit(instance)) return false;
  delete instance;
  return true;
}

bool setup_mpu6050_instance(Mpu6050ModuleInstance *instance,
                            const Mpu6050ModuleConfig &config) {
  if (instance == nullptr) {
    return false;
  }

  std::lock_guard<std::recursive_mutex> lock(instance->mutex);
  if (!release_resources(*instance)) return false;
  reset_runtime_state(*instance, config);

  if (!setup_custom_i2c_transport(*instance)) {
    ESP_LOGE(kLogTag, "init failed: %s", instance->last_error.c_str());
    return fail_setup(*instance);
  }

  if (!detect_device(*instance)) {
    ESP_LOGW(kLogTag, "init skipped: %s", instance->last_error.c_str());
    return fail_setup(*instance);
  }

  if (!configure_device(*instance)) {
    ESP_LOGE(kLogTag, "init failed: %s", instance->last_error.c_str());
    return fail_setup(*instance);
  }

  if (config.enable_pose && config.calibrate_gyro_on_setup) {
    std::string calibration_error;
    if (!calibrate_gyro_offsets(*instance, config.gyro_calibration_samples,
                                calibration_error)) {
      instance->last_error = calibration_error;
      ESP_LOGE(kLogTag, "gyro calibration failed: %s",
           instance->last_error.c_str());
      return fail_setup(*instance);
    }
  }

  instance->initialized = true;
  ESP_LOGI(kLogTag, "ready address=0x%02X external_bus=%u sda=%u scl=%u",
       static_cast<unsigned>(instance->i2c_address),
       config.external_i2c_bus != nullptr ? 1U : 0U,
       static_cast<unsigned>(config.sda_pin),
       static_cast<unsigned>(config.scl_pin));
  return true;
}

bool mpu6050_instance_deinit(Mpu6050ModuleInstance *instance) {
  if (instance == nullptr) {
    return true;
  }

  std::lock_guard<std::recursive_mutex> lock(instance->mutex);
  if (!release_resources(*instance)) return false;
  reset_runtime_state(*instance, Mpu6050ModuleConfig());
  return true;
}

void tick_mpu6050_instance(Mpu6050ModuleInstance *instance) {
  if (instance == nullptr) {
    return;
  }

  std::lock_guard<std::recursive_mutex> lock(instance->mutex);
  if (!instance->initialized || !instance->sampling) {
    return;
  }

  const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  if (instance->last_tick_ms != 0 &&
      now - instance->last_tick_ms < instance->sample_interval_ms) {
    return;
  }

  instance->last_tick_ms = now;
  read_sample(*instance);
}

bool mpu6050_instance_read_now(Mpu6050ModuleInstance *instance) {
  if (instance == nullptr) {
    return false;
  }

  std::lock_guard<std::recursive_mutex> lock(instance->mutex);
  if (!instance->initialized) {
    instance->last_error = "sensor_not_initialized";
    return false;
  }
  if (instance->sleeping) {
    instance->last_error = "sensor_sleeping";
    return false;
  }
  return read_sample(*instance);
}

bool mpu6050_instance_available(Mpu6050ModuleInstance *instance) {
  if (instance == nullptr) {
    return false;
  }

  std::lock_guard<std::recursive_mutex> lock(instance->mutex);
  return instance->initialized;
}

Mpu6050DiagnosticFacts mpu6050_instance_diagnostic_facts(Mpu6050ModuleInstance *instance) {
  Mpu6050DiagnosticFacts facts;
  if (!instance) return facts;
  std::lock_guard<std::recursive_mutex> lock(instance->mutex);
  facts.initialized = instance->initialized;
  facts.bus_pins_known = instance->initialized && instance->owns_i2c_bus;
  facts.sda_pin = instance->config.sda_pin;
  facts.scl_pin = instance->config.scl_pin;
  facts.has_interrupt_pin = instance->config.has_interrupt_pin;
  facts.interrupt_pin = instance->config.interrupt_pin;
  facts.address = instance->i2c_address;
  facts.clock_hz = instance->config.i2c_clock_hz;
  facts.sample_rate_hz = instance->sample_rate_hz;
  return facts;
}

Mpu6050Status mpu6050_instance_status(Mpu6050ModuleInstance *instance) {
  if (instance == nullptr) {
    Mpu6050Status status;
    status.last_error = "invalid_instance";
    return status;
  }

  std::lock_guard<std::recursive_mutex> lock(instance->mutex);
  return make_status_locked(*instance);
}

Mpu6050Pose mpu6050_instance_pose(Mpu6050ModuleInstance *instance) {
  if (instance == nullptr) {
    return {};
  }

  std::lock_guard<std::recursive_mutex> lock(instance->mutex);
  return instance->pose;
}

bool mpu6050_instance_calibrate_gyro(Mpu6050ModuleInstance *instance,
                                     uint16_t samples, std::string &error) {
  if (instance == nullptr) {
    error = "invalid_instance";
    return false;
  }

  std::lock_guard<std::recursive_mutex> lock(instance->mutex);
  if (!instance->initialized) {
    error = "sensor_not_initialized";
    return false;
  }

  const bool was_sampling = instance->sampling;
  instance->sampling = false;
  const bool ok = calibrate_gyro_offsets(*instance, samples, error);
  instance->sampling = was_sampling;
  return ok;
}

void mpu6050_instance_reset_pose(Mpu6050ModuleInstance *instance) {
  if (instance == nullptr) {
    return;
  }

  std::lock_guard<std::recursive_mutex> lock(instance->mutex);
  reset_pose_state(*instance);
}

bool mpu6050_instance_start_sampling(Mpu6050ModuleInstance *instance,
                                     std::string &error) {
  if (instance == nullptr) {
    error = "invalid_instance";
    return false;
  }

  std::lock_guard<std::recursive_mutex> lock(instance->mutex);
  if (!instance->initialized) {
    error = "sensor_not_initialized";
    return false;
  }
  if (!mpu6050_instance_set_sleep(instance, false, error)) return false;
  instance->sampling = true;
  instance->last_tick_ms = 0;
  error.clear();
  return true;
}

void mpu6050_instance_stop_sampling(Mpu6050ModuleInstance *instance) {
  if (instance == nullptr) {
    return;
  }

  std::lock_guard<std::recursive_mutex> lock(instance->mutex);
  instance->sampling = false;
}

bool mpu6050_instance_set_sleep(Mpu6050ModuleInstance *instance, bool sleep,
                                std::string &error) {
  if (!instance) { error = "invalid_instance"; return false; }
  std::lock_guard<std::recursive_mutex> lock(instance->mutex);
  if (!instance->initialized) { error = "sensor_not_initialized"; return false; }
  if (instance->sleeping != sleep) {
    mpu6050_power_management1_register_t power = {};
    power.bits.clock_source = MPU6050_GYRO_CS_PLL_X_AXIS_REF;
    power.bits.sleep_enabled = sleep;
    mpu6050_power_management1_register_t confirmed = {};
    if (mpu6050_set_power_management1_register(instance->sensor, power) != ESP_OK ||
        mpu6050_get_power_management1_register(instance->sensor, &confirmed) != ESP_OK ||
        confirmed.bits.sleep_enabled != sleep) {
      instance->last_error = error = "sleep_config_failed";
      return false;
    }
    instance->sleeping = sleep;
    // Never integrate across a suspension or expose a pre-suspend pose.
    instance->latest_sample.valid = false;
    reset_pose_state(*instance);
    instance->last_tick_ms = 0;
  }
  if (sleep) instance->sampling = false;
  if (instance->last_error == "sleep_config_failed" || instance->last_error == "sensor_sleeping")
    instance->last_error.clear();
  error.clear();
  return true;
}

bool mpu6050_instance_set_sample_rate_hz(Mpu6050ModuleInstance *instance,
                                         uint16_t sample_rate_hz,
                                         std::string &error) {
  if (instance == nullptr) {
    error = "invalid_instance";
    return false;
  }

  std::lock_guard<std::recursive_mutex> lock(instance->mutex);
  if (!instance->initialized) {
    error = "sensor_not_initialized";
    return false;
  }

  if (sample_rate_hz < kMinSampleRateHz || sample_rate_hz > kMaxSampleRateHz) {
    error = "sample_rate_out_of_range";
    return false;
  }

  if (!apply_sample_rate_register(*instance, sample_rate_hz)) {
    error = "sample_rate_config_failed";
    return false;
  }

  instance->sample_rate_hz = sample_rate_hz;
  instance->sample_interval_ms = static_cast<uint16_t>(1000 / sample_rate_hz);
  error.clear();
  return true;
}

Mpu6050ModuleInstance *default_mpu6050_instance() {
  // Sampling and diagnostics use this on task owners, never from an ISR or
  // while the flash cache is disabled. Keep its 64-sample history in PSRAM
  // when the product enables external BSS; other targets retain normal BSS.
#ifdef ESP_PLATFORM
  EXT_RAM_BSS_ATTR
#endif
  static Mpu6050ModuleInstance instance;
  return &instance;
}

Mpu6050Telemetry mpu6050_instance_telemetry(Mpu6050ModuleInstance *instance,
                                           uint32_t after_seq, uint16_t limit) {
  Mpu6050Telemetry result;
  if (!instance) return result;
  std::lock_guard<std::recursive_mutex> lock(instance->mutex);
  result.status = make_status_locked(*instance);
  result.buffered = std::min<uint32_t>(instance->sample_count, kTelemetryBufferSize);
  const size_t maximum = std::min<size_t>(limit ? limit : 8, result.samples.size());
  const uint16_t start = instance->sample_count < kTelemetryBufferSize ? 0 : instance->sample_ring_write_index;
  for (uint16_t offset = 0; offset < result.buffered && result.count < maximum; ++offset) {
    const auto &sample = instance->sample_ring[(start + offset) % kTelemetryBufferSize];
    if (sample.valid && sample.seq > after_seq) result.samples[result.count++] = sample;
  }
  return result;
}

} // namespace lunaredu::device
