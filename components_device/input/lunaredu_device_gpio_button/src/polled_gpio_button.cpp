#include "lunaredu_device_gpio_button_polled_gpio_button.h"
#include <driver/gpio.h>
namespace lunaredu::device {
esp_err_t PolledGpioButton::initialize(int gpio, bool active_high) {
  deinitialize();
  if (gpio < 0 || gpio >= GPIO_NUM_MAX) return ESP_ERR_INVALID_ARG;
  
  gpio_config_t config = {}; config.pin_bit_mask = 1ULL << gpio;
  config.mode = GPIO_MODE_INPUT;
  config.pull_up_en = active_high ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE;
  config.pull_down_en = active_high ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
  esp_err_t err = gpio_config(&config);
  if (err != ESP_OK) return err;
  gpio_ = gpio; active_high_ = active_high; return ESP_OK;
}
void PolledGpioButton::deinitialize() { if (ready()) {  gpio_reset_pin(static_cast<gpio_num_t>(gpio_)); gpio_ = -1; } }
bool PolledGpioButton::pressed() const { return ready() && ((gpio_get_level(static_cast<gpio_num_t>(gpio_)) != 0) == active_high_); }
}
