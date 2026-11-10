#include "sdkconfig.h"
#include <string>
#include <utility>
#include "lunaredu_device_mpu6050_imu.h"
#include "lunaredu_device_mpu6050_sampling_owner.h"
#include "mpu6050_local_api.h"

namespace {
[[maybe_unused]] constexpr const char *kDisabledError = "disabled";
[[maybe_unused]] void set_error(std::string &error, const char *message = kDisabledError) {
  error = message == nullptr ? kDisabledError : message;
}
}

#if !CONFIG_LUNAREDU_DEVICE_ENABLE_MPU6050

namespace lunaredu::device {

bool setup_mpu6050_sampling_owner(lunaros::KernelRuntime &, const Mpu6050SamplingOwnerConfig &, std::string &error) { set_error(error); return false; }
Mpu6050SamplingStatus mpu6050_sampling_status() { Mpu6050SamplingStatus result; result.sensor.last_error=kDisabledError; return result; }
Mpu6050MotionObservation mpu6050_sampling_motion() { return {}; }
bool set_mpu6050_sampling_demand(bool needed, std::string &error) {
  error = needed ? "imu_disabled" : ""; return !needed;
}
bool stop_mpu6050_sampling_owner(std::string &error,uint32_t) { error.clear();return true; }

bool declare_mpu6050_projection(lunaredu::lunaros::DeviceProjectionRegistry &,
                                std::string &error) { set_error(error); return false; }

Mpu6050ModuleInstance *mpu6050_create_instance() { return nullptr; }
bool mpu6050_destroy_instance(Mpu6050ModuleInstance *) { return true; }
bool setup_mpu6050_instance(Mpu6050ModuleInstance *,
                            const Mpu6050ModuleConfig &) {
  return false;
}
bool mpu6050_instance_deinit(Mpu6050ModuleInstance *) { return true; }
void tick_mpu6050_instance(Mpu6050ModuleInstance *) {}
bool mpu6050_instance_read_now(Mpu6050ModuleInstance *) { return false; }
bool mpu6050_instance_available(Mpu6050ModuleInstance *) { return false; }
Mpu6050Status mpu6050_instance_status(Mpu6050ModuleInstance *) {
  Mpu6050Status status;
  status.last_error = kDisabledError;
  return status;
}
Mpu6050Pose mpu6050_instance_pose(Mpu6050ModuleInstance *) { return {}; }
bool mpu6050_instance_calibrate_gyro(Mpu6050ModuleInstance *, uint16_t,
                                     std::string &error) {
  set_error(error);
  return false;
}
void mpu6050_instance_reset_pose(Mpu6050ModuleInstance *) {}
bool mpu6050_instance_start_sampling(Mpu6050ModuleInstance *,
                                     std::string &error) {
  set_error(error);
  return false;
}
void mpu6050_instance_stop_sampling(Mpu6050ModuleInstance *) {}
bool mpu6050_instance_set_sleep(Mpu6050ModuleInstance *, bool, std::string &error) {
  set_error(error); return false;
}
bool mpu6050_instance_set_sample_rate_hz(Mpu6050ModuleInstance *, uint16_t,
                                         std::string &error) {
  set_error(error);
  return false;
}
bool setup_mpu6050(const Mpu6050ModuleConfig &) { return false; }
bool setup_mpu6050_with_bus(const Mpu6050ModuleConfig &,
                            i2c_master_bus_handle_t) {
  return false;
}
bool mpu6050_deinit() { return true; }
bool setup_main_imu_module() { return false; }
void tick_mpu6050() {}
bool mpu6050_read_now() { return false; }
bool mpu6050_available() { return false; }
Mpu6050Status mpu6050_status() {
  Mpu6050Status status;
  status.last_error = kDisabledError;
  return status;
}
Mpu6050Pose mpu6050_pose() { return {}; }
bool mpu6050_calibrate_gyro(uint16_t, std::string &error) {
  set_error(error);
  return false;
}
void mpu6050_reset_pose() {}
bool mpu6050_start_sampling(std::string &error) {
  set_error(error);
  return false;
}
void mpu6050_stop_sampling() {}
bool mpu6050_set_sleep(bool, std::string &error) { set_error(error); return false; }
bool mpu6050_set_sample_rate_hz(uint16_t, std::string &error) {
  set_error(error);
  return false;
}
bool dispatch_mpu6050_status(lunaredu::lunaros::KernelRuntime &, Mpu6050Status *status, std::string &error) {
  if (status != nullptr) {
    *status = mpu6050_status();
  }
  set_error(error);
  return false;
}
bool dispatch_mpu6050_start_sampling(lunaredu::lunaros::KernelRuntime &, Mpu6050Status *status,
                                     std::string &error) {
  if (status != nullptr) {
    *status = mpu6050_status();
  }
  set_error(error);
  return false;
}
bool dispatch_mpu6050_stop_sampling(lunaredu::lunaros::KernelRuntime &, Mpu6050Status *status,
                                    std::string &error) {
  if (status != nullptr) {
    *status = mpu6050_status();
  }
  set_error(error);
  return false;
}
bool dispatch_mpu6050_set_sample_rate(lunaredu::lunaros::KernelRuntime &, uint16_t, Mpu6050Status *status,
                                      std::string &error) {
  if (status != nullptr) {
    *status = mpu6050_status();
  }
  set_error(error);
  return false;
}

} // namespace lunaredu::device

#endif
