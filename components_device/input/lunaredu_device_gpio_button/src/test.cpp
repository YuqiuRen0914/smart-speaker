#include "lunaredu_device_gpio_button_test.h"

#include "sdkconfig.h"
#if 1
#include "lunaredu_lunaros/esp_idf/device_test.h"
#include <esp_log.h>
#include <cstdint>

namespace lunaredu::device::gpio_button {
namespace {
constexpr const char *kTag = "test_gpio_button";
PolledGpioButton button;
bool observed = false, stable = false;
uint32_t changed_ms = 0, debounce_ms = 30;
[[maybe_unused]] uint32_t reported_ms = 0;
[[maybe_unused]] uint32_t phase = 0;

esp_err_t initialize(const void *opaque) {
  [[maybe_unused]] const auto &config = *static_cast<const TestConfig *>(opaque);
  reported_ms = 0;
  phase = 0;
  debounce_ms = config.debounce_ms;
  if (debounce_ms > 1000) return ESP_ERR_INVALID_ARG;
  const auto result = button.initialize(config.gpio, config.active_high);
  observed = stable = false; changed_ms = 0;
  return result;
}

void tick([[maybe_unused]] uint32_t elapsed_ms) {
  const bool value = button.pressed();
  if (value != observed) { observed = value; changed_ms = elapsed_ms; }
  if (observed != stable && elapsed_ms - changed_ms >= debounce_ms) {
    stable = observed;
    ESP_LOGI(kTag, "%s", stable ? "pressed" : "released");
  }
}

esp_err_t shutdown() {
  button.deinitialize();
  return ESP_OK;
}

const lunaros::esp_idf::DeviceTest<initialize, tick, shutdown> definition{kTag, 20};
}  // namespace

esp_err_t start_test(const TestConfig &config) { return definition.start(&config); }
esp_err_t stop_test() { return definition.stop(); }

}  // namespace lunaredu::device::gpio_button
#else
namespace lunaredu::device::gpio_button {
esp_err_t start_test(const TestConfig &) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t stop_test() { return ESP_OK; }
}  // namespace lunaredu::device::gpio_button
#endif
