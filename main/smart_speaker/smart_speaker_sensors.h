#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <string>
#include "lunaredu_imu_service.h"

namespace smart_speaker {

constexpr size_t kSmartSpeakerI2cScanMaxAddresses = 16;


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
  uint8_t contact_count = 0;
  uint16_t x = 0;
  uint16_t y = 0;
  uint32_t sample_count = 0;
  uint32_t sampled_at_ms = 0;
  std::string last_error;
};



bool setup_smart_speaker_sensors();
void tick_smart_speaker_sensors();
void set_smart_speaker_imu_service(lunaredu::services::imu::ImuService *service);
bool start_smart_speaker_imu(std::string &error);
bool read_smart_speaker_imu(lunaredu::services::imu::ImuSample &sample,
                            std::string &error);
bool stop_smart_speaker_imu(std::string &error);
SmartSpeakerMpuSnapshot smart_speaker_mpu_snapshot();
SmartSpeakerTouchSnapshot smart_speaker_touch_snapshot();

} // namespace smart_speaker
