#include "lunaredu_device_gpio_button.h"

#include <utility>

#include <button_gpio.h>
#include <button_types.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <iot_button.h>

namespace lunaredu::device {
namespace {

constexpr const char *kLogTag = "device_gpio_button";

bool is_valid_gpio(int gpio) { return gpio >= 0 && gpio < GPIO_NUM_MAX; }

bool is_bootstrap_gpio(int gpio) {
  return gpio == GPIO_NUM_0 || gpio == GPIO_NUM_45 || gpio == GPIO_NUM_46;
}

} // namespace

DeviceGpioButton::DeviceGpioButton(DeviceGpioButtonConfig config)
    : config_(config) {}

DeviceGpioButton::~DeviceGpioButton() {
  if (handle_ != nullptr) {
    iot_button_delete(static_cast<button_handle_t>(handle_));
    handle_ = nullptr;
  }
}

esp_err_t DeviceGpioButton::Init() {
  if (handle_ != nullptr) {
    return ESP_OK;
  }

  log_gpio_button_config(config_);
  if (!is_valid_gpio(config_.gpio)) {
    ESP_LOGW(kLogTag, "%s button is disabled", config_.label);
    return ESP_ERR_NOT_FOUND;
  }

  if (is_bootstrap_gpio(config_.gpio)) {
    ESP_LOGW(kLogTag,
             "%s button uses strap GPIO%d; keep external circuitry high "
             "impedance when not pressed",
             config_.label, config_.gpio);
  }

  button_config_t button_config = {};
  button_config.long_press_time = config_.long_press_time_ms;
  button_config.short_press_time = config_.short_press_time_ms;

  button_gpio_config_t gpio_config = {};
  gpio_config.gpio_num = static_cast<gpio_num_t>(config_.gpio);
  gpio_config.active_level = static_cast<uint8_t>(config_.active_high ? 1 : 0);
  gpio_config.enable_power_save = config_.enable_power_save;
  gpio_config.disable_pull = false;

  button_handle_t button_handle = nullptr;
  const esp_err_t result =
      iot_button_new_gpio_device(&button_config, &gpio_config, &button_handle);
  if (result == ESP_OK) {
    handle_ = button_handle;
  }
  return result;
}

void DeviceGpioButton::OnPressDown(DeviceButtonCallback callback) {
  RegisterCallback(BUTTON_PRESS_DOWN, nullptr, &on_press_down_,
                   std::move(callback));
}

void DeviceGpioButton::OnPressUp(DeviceButtonCallback callback) {
  RegisterCallback(BUTTON_PRESS_UP, nullptr, &on_press_up_,
                   std::move(callback));
}

void DeviceGpioButton::OnLongPress(DeviceButtonCallback callback) {
  RegisterCallback(BUTTON_LONG_PRESS_START, nullptr, &on_long_press_,
                   std::move(callback));
}

void DeviceGpioButton::OnClick(DeviceButtonCallback callback) {
  RegisterCallback(BUTTON_SINGLE_CLICK, nullptr, &on_click_,
                   std::move(callback));
}

void DeviceGpioButton::OnDoubleClick(DeviceButtonCallback callback) {
  RegisterCallback(BUTTON_DOUBLE_CLICK, nullptr, &on_double_click_,
                   std::move(callback));
}

void DeviceGpioButton::OnMultipleClick(DeviceButtonCallback callback,
                                       uint8_t click_count) {
  button_event_args_t args = {
      .multiple_clicks =
          {
              .clicks = click_count,
          },
  };
  RegisterCallback(BUTTON_MULTIPLE_CLICK, &args, &on_multiple_click_,
                   std::move(callback));
}

esp_err_t DeviceGpioButton::RegisterCallback(int event,
                                             void *args,
                                             DeviceButtonCallback *slot,
                                             DeviceButtonCallback callback) {
  if (handle_ == nullptr) {
    ESP_LOGW(kLogTag, "%s button callback registration ignored before init",
             config_.label);
    return ESP_ERR_INVALID_STATE;
  }

  *slot = std::move(callback);
  const esp_err_t result = iot_button_register_cb(
      static_cast<button_handle_t>(handle_), static_cast<button_event_t>(event),
      static_cast<button_event_args_t *>(args),
      [](void *, void *user_data) {
        auto *registered = static_cast<DeviceButtonCallback *>(user_data);
        if (*registered) {
          (*registered)();
        }
      },
      slot);
  if (result != ESP_OK) {
    ESP_LOGW(kLogTag, "%s button callback registration failed event=%d: %s",
             config_.label, static_cast<int>(event), esp_err_to_name(result));
    *slot = nullptr;
  }
  return result;
}

DeviceGpioButtonConfig primary_button_config_from_kconfig() {
  DeviceGpioButtonConfig config;
  config.label = "primary";
#if CONFIG_LUNAREDU_DEVICE_INPUT_PRIMARY_BUTTON
  config.gpio = CONFIG_LUNAREDU_DEVICE_INPUT_PRIMARY_BUTTON_GPIO;
#ifdef CONFIG_LUNAREDU_DEVICE_INPUT_PRIMARY_BUTTON_ACTIVE_HIGH
  config.active_high = CONFIG_LUNAREDU_DEVICE_INPUT_PRIMARY_BUTTON_ACTIVE_HIGH;
#endif
  config.long_press_time_ms =
      CONFIG_LUNAREDU_DEVICE_INPUT_PRIMARY_BUTTON_LONG_PRESS_MS;
  config.short_press_time_ms =
      CONFIG_LUNAREDU_DEVICE_INPUT_PRIMARY_BUTTON_SHORT_PRESS_MS;
#ifdef CONFIG_LUNAREDU_DEVICE_INPUT_PRIMARY_BUTTON_POWER_SAVE
  config.enable_power_save =
      CONFIG_LUNAREDU_DEVICE_INPUT_PRIMARY_BUTTON_POWER_SAVE;
#endif
#else
  config.gpio = -1;
#endif
  return config;
}

DeviceGpioButtonConfig ptt_button_config_from_kconfig() {
  DeviceGpioButtonConfig config;
  config.label = "press_to_talk";
  config.long_press_time_ms = 0;
#if CONFIG_LUNAREDU_DEVICE_INPUT_PTT_BUTTON
  config.gpio = CONFIG_LUNAREDU_DEVICE_INPUT_PTT_BUTTON_GPIO;
#ifdef CONFIG_LUNAREDU_DEVICE_INPUT_PTT_BUTTON_ACTIVE_HIGH
  config.active_high = CONFIG_LUNAREDU_DEVICE_INPUT_PTT_BUTTON_ACTIVE_HIGH;
#endif
#ifdef CONFIG_LUNAREDU_DEVICE_INPUT_PTT_BUTTON_POWER_SAVE
  config.enable_power_save = CONFIG_LUNAREDU_DEVICE_INPUT_PTT_BUTTON_POWER_SAVE;
#endif
#else
  config.gpio = -1;
#endif
  return config;
}

void log_gpio_button_config(const DeviceGpioButtonConfig &config) {
  ESP_LOGI(kLogTag,
           "%s button gpio=%d active_high=%d long_press_ms=%u "
           "short_press_ms=%u power_save=%d",
           config.label, config.gpio, config.active_high,
           config.long_press_time_ms, config.short_press_time_ms,
           config.enable_power_save);
}

} // namespace lunaredu::device
