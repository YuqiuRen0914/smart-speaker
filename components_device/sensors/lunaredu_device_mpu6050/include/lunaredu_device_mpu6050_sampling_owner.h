#pragma once
#include "lunaredu_device_mpu6050_imu.h"
#include "lunaredu_device_mpu6050_timing.h"
#include "kernel/managed_task.h"
#include <array>
namespace lunaredu::device {
struct ImuBusScan {
  bool completed = false;
  int sda_idle = -1;
  int scl_idle = -1;
  int sda_after = -1;
  int scl_after = -1;
  uint16_t attempted = 0;
  uint8_t last_address = 0;
  uint8_t attempts = 0;
  int first_sda_idle = -1;
  int first_scl_idle = -1;
  std::string first_error;
  uint8_t first_count = 0;
  std::array<uint8_t, 112> addresses{};
  uint8_t count = 0;
  std::array<int, 3> identity_register_75{{-1, -1, -1}}; // 0x68, 0x69, 0x74
  uint16_t timeouts = 0;
  std::string error;
};
struct Mpu6050SamplingStatus {
  Mpu6050Status sensor;
  Mpu6050SamplingTiming timing;
  ImuBusScan bus_scan;
  bool requested = false;
};
// Hot-path motion facts from the sampling owner's published cache. Reading
// them neither starts sampling nor performs hardware I/O or copies diagnostics.
struct Mpu6050MotionObservation {
  Mpu6050Pose pose;
  bool available = false;
  bool sampling = false;
  bool healthy = false; // No driver error; diagnostics remain available separately.
  bool fresh(uint32_t observed_at_ms, uint32_t freshness_ms) const {
    return available && sampling && pose.valid &&
        uint32_t(observed_at_ms - pose.t_ms) <= freshness_ms;
  }
};
struct Mpu6050SamplingOwnerConfig {
  Mpu6050ModuleConfig sensor;
  lunaros::ManagedTaskDefinition task;
  bool diagnostic_scan = false;
  uint32_t retry_interval_ms = 5000;
};
// Registers the boot-lifetime configuration, without a worker or hardware.
bool setup_mpu6050_sampling_owner(lunaros::KernelRuntime &, const Mpu6050SamplingOwnerConfig &, std::string &);
Mpu6050SamplingStatus mpu6050_sampling_status();
Mpu6050MotionObservation mpu6050_sampling_motion();
// Failed admission does not leave an unfulfilled demand; the caller may retry.
bool set_mpu6050_sampling_demand(bool, std::string &error);
bool stop_mpu6050_sampling_owner(std::string &, uint32_t timeout_ms = 300);
}
