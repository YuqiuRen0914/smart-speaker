#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <string>

namespace smart_speaker {

constexpr size_t kSmartSpeakerI2cScanMaxAddresses = 16;

enum class TouchGesture : uint8_t {
  None,
  Tap,
  LongPress,
  LongPressRelease,
  SwipeLeft,
  SwipeRight,
  SwipeUp,
  SwipeDown,
};

struct SmartSpeakerMpuSnapshot {
  bool initialized = false;
  bool sampling = false;
  uint8_t address = 0;
  float temperature_c = 0;
  bool pose_valid = false;
  float angle_x = 0;
  float angle_y = 0;
  float angle_z = 0;
  bool scan_complete = false;
  uint8_t scan_count = 0;
  std::array<uint8_t, kSmartSpeakerI2cScanMaxAddresses> scan_addresses{};
  bool who_am_i_valid = false;
  uint8_t who_am_i = 0;
  std::string who_am_i_error;
  std::string last_error;
};

struct SmartSpeakerTouchSnapshot {
  bool initialized = false;
  bool pressed = false;
  uint8_t address = 0;
  uint16_t x = 0;
  uint16_t y = 0;
  uint32_t sample_count = 0;
  uint32_t gesture_count = 0;
  std::string last_error;
};

struct SmartSpeakerMotionInput {
  bool valid = false;
  float tilt_x = 0;
  float tilt_y = 0;
  float twist_z = 0;
  float angular_speed_dps = 0;
};

bool setup_smart_speaker_sensors();
void tick_smart_speaker_sensors();
SmartSpeakerMpuSnapshot smart_speaker_mpu_snapshot();
SmartSpeakerTouchSnapshot smart_speaker_touch_snapshot();
SmartSpeakerMotionInput smart_speaker_motion_input();
TouchGesture smart_speaker_take_touch_gesture();
const char *touch_gesture_name(TouchGesture gesture);

} // namespace smart_speaker
