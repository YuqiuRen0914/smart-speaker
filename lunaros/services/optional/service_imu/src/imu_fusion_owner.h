#pragma once
#include "lunaredu_imu_service.h"
#if defined(CONFIG_LUNAROS_IMU_FUSION) && CONFIG_LUNAROS_IMU_FUSION
namespace lunaredu::services::imu {
struct ImuService::FusionOwner {
  fusion::Filter filter;
  fusion::Config config;
  fusion::Quaternion initial{{1, 0, 0, 0}};
  MagneticSource source;
  FusionStatus status;
  bool imu_owned = false;
  bool magnetic_owned = false;
  bool stopping = false;
  // A bounded time-alignment queue. Never re-stamp a future sample or replace
  // the only usable past observation with a newly arriving future observation.
  std::array<fusion::MagneticInput, 32> magnetic_queue{};
  size_t magnetic_count = 0;
  fusion::MagneticInput magnetic_fault{};
  bool magnetic_fault_pending = false;
  std::string magnetic_error, attitude_error, cleanup_error;
};
} // namespace lunaredu::services::imu
#endif
