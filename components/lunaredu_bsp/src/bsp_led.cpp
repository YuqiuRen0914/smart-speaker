#include "bsp/lunaredu_bsp.h"

#include <driver/gpio.h>
#include <mutex>

namespace {

bool g_led_ready = false;
std::recursive_mutex g_led_mutex;
constexpr const char *kOwner = "status_led";

} // 匿名命名空间

esp_err_t bsp_status_led_init() {
  std::lock_guard<std::recursive_mutex> lock(g_led_mutex);
  if (g_led_ready) return ESP_OK;
  const esp_err_t claim =
      bsp_resource_acquire(BSP_RESOURCE_ONBOARD_STATUS_LED, kOwner);
  if (claim != ESP_OK) {
    g_led_ready = false;
    return claim;
  }

  gpio_config_t io_config = {};
  io_config.pin_bit_mask = 1ULL << lunaredu::bsp::pins::kStatusLed;
  io_config.mode = GPIO_MODE_OUTPUT;
  io_config.pull_up_en = GPIO_PULLUP_DISABLE;
  io_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_config.intr_type = GPIO_INTR_DISABLE;

  const esp_err_t result = gpio_config(&io_config);
  if (result != ESP_OK) {
    g_led_ready = false;
    bsp_resource_release(BSP_RESOURCE_ONBOARD_STATUS_LED, kOwner);
    return result;
  }

  g_led_ready = true;
  const esp_err_t off_result = bsp_status_led_set(false);
  if (off_result != ESP_OK) {
    (void)bsp_status_led_deinit();
  }
  return off_result;
}

esp_err_t bsp_status_led_set(bool on) {
  std::lock_guard<std::recursive_mutex> lock(g_led_mutex);
  if (!g_led_ready) {
    return ESP_ERR_INVALID_STATE;
  }

  const bool inverted = lunaredu::bsp::pins::kStatusLedInverted;
  const int level = inverted ? (on ? 0 : 1) : (on ? 1 : 0);
  return gpio_set_level(static_cast<gpio_num_t>(lunaredu::bsp::pins::kStatusLed),
                        level);
}

esp_err_t bsp_status_led_deinit() {
  std::lock_guard<std::recursive_mutex> lock(g_led_mutex);
  esp_err_t result = ESP_OK;

  if (g_led_ready) {
    result = bsp_status_led_set(false);
    const esp_err_t reset_result = gpio_reset_pin(
        static_cast<gpio_num_t>(lunaredu::bsp::pins::kStatusLed));
    if (result == ESP_OK) {
      result = reset_result;
    }
    if (reset_result != ESP_OK) return result;
    g_led_ready = false;
  }

  const esp_err_t release_result =
      bsp_resource_release(BSP_RESOURCE_ONBOARD_STATUS_LED, kOwner);
  return result != ESP_OK ? result : release_result;
}
