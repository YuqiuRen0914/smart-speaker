#include "boards/board_descriptor.h"
#include "sdkconfig.h"
#include "bsp/boards/remote_controller.h"

namespace lunaredu::bsp {
bool uses_legacy_baseboard() {
#if CONFIG_LUNAROS_BSP_ENABLE_LEGACY_BASEBOARD
  switch (bsp_board_profile()) {
  case BSP_BOARD_PROFILE_BASE:
  case BSP_BOARD_PROFILE_CAM_BUNDLE:
  case BSP_BOARD_PROFILE_MINIMAL:
    return true;
  default:
    break;
  }
#endif
  return false;
}

const ResourceTable &board_resources() {
  static const ResourceTable table = [] {
    if (uses_legacy_baseboard()) return legacy_baseboard_resources();
    ResourceTable result{};
    constexpr const char *names[] = {
      "baseboard_mpu6050_i2c", "expansion_i2c1", "expansion_spi1",
      "onboard_ws2812", "onboard_status_led", "camera", "tfcard",
      "usb_native", "i2s_data", "pwm_servo_bank", "pwm_motor_bank",
      "bat_adc", "xiaoxing_i2s_audio", "remote_controller_touch"};
    static_assert(sizeof(names) / sizeof(names[0]) == BSP_RESOURCE_COUNT);
    for (int index = 0; index < BSP_RESOURCE_COUNT; ++index) {
      result[index] = {static_cast<bsp_resource_t>(index), names[index], 0, false};
    }
    if (bsp_board_profile() == BSP_BOARD_PROFILE_XIAOXING) {
      // Existing XiaoXing wiring contract; this does not claim hardware validation.
      result[BSP_RESOURCE_BAT_ADC].pin_mask = 1ULL << xiaoxing_power::battery_adc_pin;
      result[BSP_RESOURCE_BAT_ADC].available = true;
      result[BSP_RESOURCE_XIAOXING_I2S_AUDIO].pin_mask =
          (1ULL << 47) | (1ULL << 14) | (1ULL << 1) | (1ULL << 21);
      result[BSP_RESOURCE_XIAOXING_I2S_AUDIO].available = true;
    }
    if (bsp_board_profile() == BSP_BOARD_PROFILE_REMOTE_CONTROLLER) {
      result[BSP_RESOURCE_BAT_ADC] = {BSP_RESOURCE_BAT_ADC, "bat_adc", 1ULL << xiaoxing_power::battery_adc_pin, true};
      result[BSP_RESOURCE_REMOTE_CONTROLLER_TOUCH] = {
          BSP_RESOURCE_REMOTE_CONTROLLER_TOUCH, "remote_controller_touch",
          (1ULL << remote_controller::kTouchSdaPin) | (1ULL << remote_controller::kTouchSclPin), true};
    }
    return result;
  }();
  return table;
}
} // namespace lunaredu::bsp

const bsp_baseboard_group_t *bsp_baseboard_groups(size_t *count) {
  if (lunaredu::bsp::uses_legacy_baseboard()) {
    return lunaredu::bsp::legacy_baseboard_groups(count);
  }
  if (count) *count = 0;
  return nullptr;
}
const bsp_i2c_bus_t *bsp_mpu_i2c_bus() {
  return lunaredu::bsp::uses_legacy_baseboard()
      ? lunaredu::bsp::legacy_mpu_i2c_bus() : nullptr;
}
const bsp_i2c_bus_t *bsp_expansion_i2c1_bus() {
  return lunaredu::bsp::uses_legacy_baseboard()
      ? lunaredu::bsp::legacy_expansion_i2c1_bus() : nullptr;
}
int bsp_battery_adc_pin() {
  const auto &descriptor = lunaredu::bsp::board_resources()[BSP_RESOURCE_BAT_ADC];
  if (!descriptor.available) return -1;
  for (int pin = 0; pin < 64; ++pin) {
    if (descriptor.pin_mask & (1ULL << pin)) return pin;
  }
  return -1;
}
const char *bsp_board_support_status() {
  if (lunaredu::bsp::uses_legacy_baseboard()) return "legacy";
  return bsp_board_profile() == BSP_BOARD_PROFILE_NONE ? "unspecified" : "product";
}

size_t bsp_resource_signals(bsp_resource_t resource, bsp_signal_observation_t *out, size_t capacity) {
  if (resource < 0 || resource >= BSP_RESOURCE_COUNT) return 0;
  const auto &descriptor = lunaredu::bsp::board_resources()[resource];
  if (!descriptor.available) return 0;
  size_t count = 0;
  const auto add = [&](const char *role, int pin) {
    if (pin < 0 || pin >= 64 || !(descriptor.pin_mask & (uint64_t{1} << pin))) return;
    if (out && count < capacity) out[count] = {role, pin};
    ++count;
  };
  const bsp_i2c_bus_t *bus = resource == BSP_RESOURCE_BASEBOARD_MPU6050_I2C
      ? bsp_mpu_i2c_bus() : resource == BSP_RESOURCE_EXPANSION_I2C1 ? bsp_expansion_i2c1_bus() : nullptr;
  if (bus) { add("sda", bus->sda_pin); add("scl", bus->scl_pin); }
  if (resource == BSP_RESOURCE_USB_NATIVE) { add("data_minus", 19); add("data_plus", 20); }
  const char *single_role = resource == BSP_RESOURCE_BAT_ADC ? "analog_voltage" :
      resource == BSP_RESOURCE_ONBOARD_WS2812 ? "digital_data" :
      resource == BSP_RESOURCE_ONBOARD_STATUS_LED ? "digital_output" : nullptr;
  // A single-signal resource must declare exactly one configured pin.
  if (single_role && descriptor.pin_mask && !(descriptor.pin_mask & (descriptor.pin_mask - 1)))
    for (int pin = 0; pin < 64; ++pin) if (descriptor.pin_mask & (uint64_t{1} << pin)) add(single_role, pin);
  return count;
}
