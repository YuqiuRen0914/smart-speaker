#pragma once
#include <esp_err.h>
namespace lunaredu::device {
class PolledGpioButton final {
public:
  PolledGpioButton() = default;
  ~PolledGpioButton() { deinitialize(); }
  PolledGpioButton(const PolledGpioButton &) = delete;
  PolledGpioButton &operator=(const PolledGpioButton &) = delete;
  esp_err_t initialize(int gpio, bool active_high);
  void deinitialize();
  bool ready() const { return gpio_ >= 0; }
  bool pressed() const;
private:
  int gpio_ = -1;
  bool active_high_ = true;
};
}
