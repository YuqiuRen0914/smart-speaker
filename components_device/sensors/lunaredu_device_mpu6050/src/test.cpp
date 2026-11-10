#include "lunaredu_device_mpu6050_test.h"

#include "sdkconfig.h"
#if CONFIG_LUNAREDU_DEVICE_ENABLE_MPU6050
#include "lunaredu_lunaros/esp_idf/device_test.h"
#include <esp_log.h>
#include <cstdint>

namespace lunaredu::device::mpu6050 {
namespace {
constexpr const char *kTag = "test_mpu6050";
Mpu6050ModuleInstance *instance = nullptr;
[[maybe_unused]] uint32_t reported_ms = 0;
[[maybe_unused]] uint32_t phase = 0;

esp_err_t initialize(const void *opaque) {
  [[maybe_unused]] const auto &config = *static_cast<const TestConfig *>(opaque);
  reported_ms = 0;
  phase = 0;
  if (!instance) instance = mpu6050_create_instance();
  if (!instance) return ESP_ERR_NO_MEM;
  return setup_mpu6050_instance(instance, config) ? ESP_OK : ESP_FAIL;
}

void tick([[maybe_unused]] uint32_t elapsed_ms) {
  (void)mpu6050_instance_read_now(instance);
  if (elapsed_ms - reported_ms < 1000) return;
  reported_ms = elapsed_ms;
  const auto s = mpu6050_instance_status(instance);
  ESP_LOGI(kTag, "ok=%d valid=%d accel=%.3f,%.3f,%.3fg gyro=%.2f,%.2f,%.2fdps error=%s", s.ok, s.latest.valid, static_cast<double>(s.latest.accel_g_x), static_cast<double>(s.latest.accel_g_y), static_cast<double>(s.latest.accel_g_z), static_cast<double>(s.latest.gyro_dps_x), static_cast<double>(s.latest.gyro_dps_y), static_cast<double>(s.latest.gyro_dps_z), s.last_error.c_str());
}

esp_err_t shutdown() {
  if (instance && !mpu6050_destroy_instance(instance)) return ESP_FAIL;
  instance = nullptr;
  return ESP_OK;
}

const lunaros::esp_idf::DeviceTest<initialize, tick, shutdown> definition{kTag, 20};
}  // namespace

esp_err_t start_test(const TestConfig &config) { return definition.start(&config); }
esp_err_t stop_test() { return definition.stop(); }

}  // namespace lunaredu::device::mpu6050
#else
namespace lunaredu::device::mpu6050 {
esp_err_t start_test(const TestConfig &) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t stop_test() { return ESP_OK; }
}  // namespace lunaredu::device::mpu6050
#endif
