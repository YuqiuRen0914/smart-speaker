#include "sdkconfig.h"
#include <string>
#include <utility>
#include "lunaredu_device_gpio_button.h"

namespace {
[[maybe_unused]] constexpr const char *kDisabledError = "disabled";
[[maybe_unused]] void set_error(std::string &error, const char *message = kDisabledError) {
  error = message == nullptr ? kDisabledError : message;
}
}

#if !CONFIG_LUNAREDU_DEVICE_INPUT_PRIMARY_BUTTON && \
    !CONFIG_LUNAREDU_DEVICE_INPUT_PTT_BUTTON

namespace lunaredu::device {

DeviceGpioButton::DeviceGpioButton(DeviceGpioButtonConfig config)
    : config_(config) {}

DeviceGpioButton::~DeviceGpioButton() = default;

esp_err_t DeviceGpioButton::Init() { return ESP_ERR_NOT_SUPPORTED; }

void DeviceGpioButton::OnPressDown(DeviceButtonCallback callback) {
  RegisterCallback(0, nullptr, &on_press_down_, std::move(callback));
}

void DeviceGpioButton::OnPressUp(DeviceButtonCallback callback) {
  RegisterCallback(0, nullptr, &on_press_up_, std::move(callback));
}

void DeviceGpioButton::OnLongPress(DeviceButtonCallback callback) {
  RegisterCallback(0, nullptr, &on_long_press_, std::move(callback));
}

void DeviceGpioButton::OnClick(DeviceButtonCallback callback) {
  RegisterCallback(0, nullptr, &on_click_, std::move(callback));
}

void DeviceGpioButton::OnDoubleClick(DeviceButtonCallback callback) {
  RegisterCallback(0, nullptr, &on_double_click_, std::move(callback));
}

void DeviceGpioButton::OnMultipleClick(DeviceButtonCallback callback, uint8_t) {
  RegisterCallback(0, nullptr, &on_multiple_click_, std::move(callback));
}

esp_err_t DeviceGpioButton::RegisterCallback(int, void *,
                                             DeviceButtonCallback *slot,
                                             DeviceButtonCallback callback) {
  if (slot != nullptr) {
    *slot = std::move(callback);
  }
  return ESP_ERR_NOT_SUPPORTED;
}

DeviceGpioButtonConfig primary_button_config_from_kconfig() {
  DeviceGpioButtonConfig config;
  config.label = "primary";
  config.gpio = -1;
  return config;
}

DeviceGpioButtonConfig ptt_button_config_from_kconfig() {
  DeviceGpioButtonConfig config;
  config.label = "press_to_talk";
  config.gpio = -1;
  config.long_press_time_ms = 0;
  return config;
}

void log_gpio_button_config(const DeviceGpioButtonConfig &) {}

} // namespace lunaredu::device

#endif
