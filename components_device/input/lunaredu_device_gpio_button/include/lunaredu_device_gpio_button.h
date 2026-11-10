#pragma once

#include <cstdint>
#include <functional>

#include <esp_err.h>

namespace lunaredu::device {

using DeviceButtonCallback = std::function<void()>;

struct DeviceGpioButtonConfig {
  int gpio = -1;
  bool active_high = false;
  uint16_t long_press_time_ms = 2000;
  uint16_t short_press_time_ms = 0;
  bool enable_power_save = false;
  const char *label = "primary";
};

class DeviceGpioButton {
public:
  explicit DeviceGpioButton(DeviceGpioButtonConfig config);
  ~DeviceGpioButton();

  DeviceGpioButton(const DeviceGpioButton &) = delete;
  DeviceGpioButton &operator=(const DeviceGpioButton &) = delete;

  esp_err_t Init();
  bool ready() const { return handle_ != nullptr; }

  void OnPressDown(DeviceButtonCallback callback);
  void OnPressUp(DeviceButtonCallback callback);
  void OnLongPress(DeviceButtonCallback callback);
  void OnClick(DeviceButtonCallback callback);
  void OnDoubleClick(DeviceButtonCallback callback);
  void OnMultipleClick(DeviceButtonCallback callback, uint8_t click_count = 3);

private:
  esp_err_t RegisterCallback(int event, void *args,
                             DeviceButtonCallback *slot,
                             DeviceButtonCallback callback);

  DeviceGpioButtonConfig config_;
  void *handle_ = nullptr;
  DeviceButtonCallback on_press_down_;
  DeviceButtonCallback on_press_up_;
  DeviceButtonCallback on_long_press_;
  DeviceButtonCallback on_click_;
  DeviceButtonCallback on_double_click_;
  DeviceButtonCallback on_multiple_click_;
};

DeviceGpioButtonConfig primary_button_config_from_kconfig();
DeviceGpioButtonConfig ptt_button_config_from_kconfig();
void log_gpio_button_config(const DeviceGpioButtonConfig &config);

} // namespace lunaredu::device
