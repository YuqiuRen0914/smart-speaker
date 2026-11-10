#include "lunaredu_device_mpu6050_example.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#if CONFIG_LUNAREDU_DEVICE_ENABLE_MPU6050
#include "lunaredu_device_mpu6050_imu.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <cstdint>
#include <initializer_list>


namespace lunaredu::device::mpu6050 {
namespace {
using ExampleConfig = Mpu6050ModuleConfig;
constexpr const char *kTag = "example_mpu6050";
Mpu6050ModuleInstance *instance = nullptr;
[[maybe_unused]] uint32_t reported_ms = 0;
[[maybe_unused]] uint32_t phase = 0;

esp_err_t initialize(const void *opaque) {
  [[maybe_unused]] const auto &config = *static_cast<const ExampleConfig *>(opaque);
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


// This blocking example runs on the caller's ESP-IDF task, without an OS service.
std::atomic_flag running = ATOMIC_FLAG_INIT;
bool cleanup_pending = false;
esp_err_t run_example(const ExampleConfig &config) {
  if (running.test_and_set(std::memory_order_acquire)) return ESP_ERR_INVALID_STATE;
  if (cleanup_pending) {
    const auto retry = shutdown();
    if (retry != ESP_OK) { running.clear(std::memory_order_release); return retry; }
    cleanup_pending = false;
  }
  const auto result = initialize(&config);
  if (result == ESP_OK) {
    const int64_t started_us = esp_timer_get_time();
    constexpr uint32_t duration_ms = 10000;
    constexpr uint32_t interval_ms = 20;
    while (static_cast<uint64_t>(esp_timer_get_time() - started_us) < duration_ms * 1000ULL) {
      tick(static_cast<uint32_t>((esp_timer_get_time() - started_us) / 1000));
      const TickType_t delay = pdMS_TO_TICKS(interval_ms);
      vTaskDelay(delay > 0 ? delay : 1);
    }
  }
  const auto cleanup = shutdown();
  cleanup_pending = cleanup != ESP_OK;
  running.clear(std::memory_order_release);
  // Initialization errors retain their native code, even if rollback also fails.
  return result != ESP_OK ? result : cleanup;
}

} // namespace

esp_err_t example(int sda_gpio, int scl_gpio) {
  if (!GPIO_IS_VALID_GPIO(sda_gpio)) return ESP_ERR_INVALID_ARG;
  if (!GPIO_IS_VALID_GPIO(scl_gpio)) return ESP_ERR_INVALID_ARG;

  if (sda_gpio < 0 || sda_gpio > 255 || scl_gpio < 0 || scl_gpio > 255) return ESP_ERR_INVALID_ARG;
  ExampleConfig config; config.sda_pin = static_cast<uint8_t>(sda_gpio); config.scl_pin = static_cast<uint8_t>(scl_gpio); config.use_bsp_i2c = false;
  
  return run_example(config);
}
} // namespace lunaredu::device::mpu6050
#else
namespace lunaredu::device::mpu6050 {
esp_err_t example([[maybe_unused]] int sda_gpio, [[maybe_unused]] int scl_gpio) { return ESP_ERR_NOT_SUPPORTED; }
}
#endif
