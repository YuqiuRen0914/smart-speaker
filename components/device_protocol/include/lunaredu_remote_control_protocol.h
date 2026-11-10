#pragma once

#include <stddef.h>
#include <stdint.h>

namespace lunaredu::remote_control {

// Payload models only. Wire bytes, version and message types live in the LP codec.


constexpr uint8_t kDigitalLevelLeftStick = 1U << 0;
constexpr uint8_t kDigitalLevelRightStick = 1U << 1;
constexpr uint8_t kDigitalLevelButtonA = 1U << 2;
constexpr uint8_t kDigitalLevelButtonB = 1U << 3;
constexpr uint8_t kDigitalLevelEncoderA = 1U << 4;
constexpr uint8_t kDigitalLevelEncoderB = 1U << 5;
constexpr int16_t kAxisMinimum = -1000;
constexpr int16_t kAxisMaximum = 1000;
constexpr uint16_t kControllerButtonLeftStick = 1U << 0;
constexpr uint16_t kControllerButtonRightStick = 1U << 1;
constexpr uint16_t kControllerButtonA = 1U << 2;
constexpr uint16_t kControllerButtonB = 1U << 3;

struct ControllerButtonPolarity {
  bool left_stick_active_high = false;
  bool right_stick_active_high = false;
  bool button_a_active_high = false;
  bool button_b_active_high = false;
};

constexpr bool digital_level_active(uint8_t levels, uint8_t level_mask,
                                    bool active_high) {
  const bool high = (levels & level_mask) != 0;
  return active_high ? high : !high;
}

constexpr uint16_t button_mask_from_digital_levels(
    uint8_t levels, const ControllerButtonPolarity &polarity) {
  return (digital_level_active(levels, kDigitalLevelLeftStick,
                               polarity.left_stick_active_high)
              ? kControllerButtonLeftStick
              : 0) |
         (digital_level_active(levels, kDigitalLevelRightStick,
                               polarity.right_stick_active_high)
              ? kControllerButtonRightStick
              : 0) |
         (digital_level_active(levels, kDigitalLevelButtonA,
                               polarity.button_a_active_high)
              ? kControllerButtonA
              : 0) |
         (digital_level_active(levels, kDigitalLevelButtonB,
                               polarity.button_b_active_high)
              ? kControllerButtonB
              : 0);
}

enum class DeviceRole : uint8_t {
  Receiver = 1,
  Controller = 2,
};

enum class PairingMessage : uint8_t {
  Request = 1,
  Accept = 2,
  Reject = 3,
};

enum class LinkState : uint8_t {
  Unpaired = 0,
  Pairing = 1,
  Linked = 2,
  Lost = 3,
  Error = 4,
};

enum class CalibrationCommand : uint8_t {
  Center = 1,
  RangeBegin = 2,
  RangeFinish = 3,
  Cancel = 4,
  Reset = 5,
};

struct InputFrame {
  uint16_t flags = 0;
  uint32_t sequence = 0;
  uint32_t timestamp_ms = 0;
  int16_t left_x = 0;
  int16_t left_y = 0;
  int16_t right_x = 0;
  int16_t right_y = 0;
  int16_t encoder_delta = 0;
  int16_t encoder_position = 0;
  uint16_t buttons = 0;
  uint16_t reserved = 0;
  uint64_t binding_nonce = 0;
};

struct PairingFrame {
  uint8_t message = static_cast<uint8_t>(PairingMessage::Request);
  uint8_t role = static_cast<uint8_t>(DeviceRole::Controller);
  uint8_t channel = 1;
  uint8_t reserved[3] = {};
  uint64_t binding_nonce = 0;
  uint32_t timestamp_ms = 0;
  uint32_t config_revision = 0;
};

// reserved0 capability/state bits. Bit 0 is presentation support.
constexpr uint8_t kLinkRemoteStopLatched = 1U << 1;
constexpr uint8_t kLinkRemoteControlActive = 1U << 2;
// LinkStatusFrame reserved[1..4] carry an optional receiver battery reading.
// These fields retain the original payload's reserved-byte assignments.
constexpr uint8_t kLinkDeviceBatteryValid = 1U << 0;

struct LinkStatusFrame {
  uint8_t state = static_cast<uint8_t>(LinkState::Unpaired);
  uint8_t reserved0 = 0;
  uint64_t binding_nonce = 0;
  uint32_t accepted_frames = 0;
  uint32_t rejected_frames = 0;
  int8_t rssi = 0;
  // reserved[0]: receiver-reported throttle mode, 0 unknown, 1/2/3 low/medium/high.
  // reserved[1] bit 0: receiver battery valid.
  // reserved[2]: receiver battery percentage, clamped to 0..100.
  // reserved[3..4]: receiver battery millivolts, little endian.
  uint8_t reserved[7] = {};
};

constexpr bool link_device_battery_valid(const LinkStatusFrame &frame) {
  return (frame.reserved[1] & kLinkDeviceBatteryValid) != 0 &&
         frame.reserved[2] <= 100;
}

constexpr uint8_t link_device_battery_percent(const LinkStatusFrame &frame) {
  return link_device_battery_valid(frame) ? frame.reserved[2] : 0;
}

constexpr uint16_t link_device_battery_millivolts(
    const LinkStatusFrame &frame) {
  return link_device_battery_valid(frame)
             ? static_cast<uint16_t>(frame.reserved[3]) |
                   (static_cast<uint16_t>(frame.reserved[4]) << 8)
             : 0;
}

struct CalibrationCommandFrame {
  uint8_t command = static_cast<uint8_t>(CalibrationCommand::Center);
  uint8_t reserved = 0;
  uint32_t sequence = 0;
  uint32_t timestamp_ms = 0;
  uint64_t binding_nonce = 0;
};

struct ControllerStatusFrame {
  uint8_t calibration_state = 0;
  uint8_t calibration_error = 0;
  uint32_t sequence = 0;
  uint32_t last_command_sequence = 0;
  uint32_t timestamp_ms = 0;
  uint32_t profile_revision = 0;
  uint64_t binding_nonce = 0;
  uint16_t raw_axes[4] = {};
  int16_t normalized_axes[4] = {};
  uint16_t minimum[4] = {};
  uint16_t center[4] = {};
  uint16_t maximum[4] = {};
  uint16_t deadzone[4] = {};
  uint16_t sample_count = 0;
  uint8_t profile_valid = 0;
  uint8_t digital_levels = 0;
};


constexpr int16_t clamp_axis(int32_t value) {
  return value < kAxisMinimum
             ? kAxisMinimum
             : (value > kAxisMaximum ? kAxisMaximum
                                      : static_cast<int16_t>(value));
}

constexpr bool valid_input_frame(const InputFrame &frame) {
  return frame.left_x >= kAxisMinimum &&
         frame.left_x <= kAxisMaximum && frame.left_y >= kAxisMinimum &&
         frame.left_y <= kAxisMaximum && frame.right_x >= kAxisMinimum &&
         frame.right_x <= kAxisMaximum && frame.right_y >= kAxisMinimum &&
         frame.right_y <= kAxisMaximum && frame.binding_nonce != 0;
}

constexpr bool valid_pairing_frame(const PairingFrame &frame) {
  return frame.channel >= 1 &&
         frame.channel <= 14 && frame.binding_nonce != 0 &&
         (frame.role == static_cast<uint8_t>(DeviceRole::Receiver) ||
          frame.role == static_cast<uint8_t>(DeviceRole::Controller)) &&
         frame.message >= static_cast<uint8_t>(PairingMessage::Request) &&
         frame.message <= static_cast<uint8_t>(PairingMessage::Reject);
}

constexpr bool valid_link_status_frame(const LinkStatusFrame &frame) {
  return frame.binding_nonce != 0 &&
         frame.state <= static_cast<uint8_t>(LinkState::Error);
}

constexpr bool valid_calibration_command_frame(
    const CalibrationCommandFrame &frame) {
  return frame.sequence != 0 &&
         frame.binding_nonce != 0 &&
         frame.command >= static_cast<uint8_t>(CalibrationCommand::Center) &&
         frame.command <= static_cast<uint8_t>(CalibrationCommand::Reset);
}

constexpr bool valid_controller_status_frame(
    const ControllerStatusFrame &frame) {
  if (frame.binding_nonce == 0 ||
      frame.calibration_state > 4 || frame.calibration_error > 5) {
    return false;
  }
  for (size_t index = 0; index < 4; ++index) {
    if (frame.raw_axes[index] > 4095 || frame.minimum[index] > 4095 ||
        frame.center[index] > 4095 || frame.maximum[index] > 4095 ||
        frame.deadzone[index] > 4095 ||
        frame.normalized_axes[index] < kAxisMinimum ||
        frame.normalized_axes[index] > kAxisMaximum) {
      return false;
    }
  }
  return true;
}

constexpr bool button_pressed(const InputFrame &frame, uint16_t mask) {
  return (frame.buttons & mask) != 0;
}

}  // namespace lunaredu::remote_control
