#pragma once

#include "bsp/boards/xiaoxing_power.h"
#include <array>
#include <cstddef>

namespace lunaredu::bsp::remote_controller {

inline constexpr int kJoystick1XPin = 7;
inline constexpr int kJoystick1YPin = 8;
inline constexpr int kJoystick1SwitchPin = 10;
inline constexpr int kJoystick2XPin = 6;
inline constexpr int kJoystick2YPin = 5;
inline constexpr int kJoystick2SwitchPin = 4;
inline constexpr int kEncoderAPin = 11;
inline constexpr int kEncoderBPin = 12;
inline constexpr int kPhysicalButtonAPin = 40;
inline constexpr int kPhysicalButtonBPin = 18;
inline constexpr int kTouchSdaPin = 43;
inline constexpr int kTouchSclPin = 44;
inline constexpr int kTouchInterruptPin = 48;
// GPIO48 belongs to FT6236 INT, never to an LED output.
inline constexpr int kStatusLedPin = -1;
inline constexpr int kImuSdaPin = 41;
inline constexpr int kImuSclPin = 42;
inline constexpr int kImuPreferredI2cAddress = 0x74;
inline constexpr int kBatteryAdcPin = xiaoxing_power::battery_adc_pin;

inline constexpr std::array<int, 10> kInputPins = {
    kJoystick1XPin, kJoystick1YPin, kJoystick1SwitchPin,
    kJoystick2XPin, kJoystick2YPin, kJoystick2SwitchPin,
    kEncoderAPin, kEncoderBPin, kPhysicalButtonBPin, kPhysicalButtonAPin};

constexpr bool input_pin(int pin) {
  for (int input : kInputPins) {
    if (input == pin) return true;
  }
  return false;
}

constexpr bool unique_input_pins() {
  for (size_t left = 0; left < kInputPins.size(); ++left) {
    for (size_t right = left + 1; right < kInputPins.size(); ++right) {
      if (kInputPins[left] == kInputPins[right]) return false;
    }
  }
  return true;
}

constexpr bool reserved_pin(int pin) {
  return input_pin(pin) || pin == kImuSdaPin || pin == kImuSclPin ||
         pin == kBatteryAdcPin || pin == kTouchSdaPin || pin == kTouchSclPin ||
         pin == kTouchInterruptPin;
}

static_assert(unique_input_pins(), "Controller inputs must use distinct GPIOs");
static_assert(!input_pin(kBatteryAdcPin) && kBatteryAdcPin != kImuSdaPin &&
              kBatteryAdcPin != kImuSclPin, "Battery ADC conflicts with an input");
static_assert(!input_pin(kStatusLedPin), "Status LED conflicts with an input");
static_assert(!input_pin(kImuSdaPin) && !input_pin(kImuSclPin) &&
              kImuSdaPin != kImuSclPin, "IMU conflicts with an input");
constexpr bool touch_pin_available(int pin) {
  return !input_pin(pin) && pin != kImuSdaPin && pin != kImuSclPin &&
         pin != kBatteryAdcPin && pin != kStatusLedPin;
}
static_assert(touch_pin_available(kTouchSdaPin) &&
              touch_pin_available(kTouchSclPin) &&
              touch_pin_available(kTouchInterruptPin) &&
              kTouchSdaPin != kTouchSclPin &&
              kTouchSdaPin != kTouchInterruptPin &&
              kTouchSclPin != kTouchInterruptPin,
              "FT6236 conflicts with another controller function");

}  // namespace lunaredu::bsp::remote_controller
