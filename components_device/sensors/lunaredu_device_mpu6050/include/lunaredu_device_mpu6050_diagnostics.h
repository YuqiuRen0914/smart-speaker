#pragma once
#include "lunaredu_device_mpu6050_sampling_owner.h"
#include <ArduinoJson.h>
namespace lunaredu::device {
inline void write_mpu6050_diagnostics(JsonObject payload,const Mpu6050SamplingStatus &snapshot,const Mpu6050ModuleConfig &config) {
  const auto &imu_status = snapshot.sensor;
  JsonObject imu = payload;
  imu["sda_gpio"] = config.sda_pin;
  imu["scl_gpio"] = config.scl_pin;
  imu["preferred_i2c_address"] = config.preferred_address;
  imu["initialized"] = imu_status.initialized;
  imu["sampling"] = imu_status.sampling;
  imu["requested"] = snapshot.requested;
  imu["sleeping"] = imu_status.sleeping;
  imu["configured_rate_hz"] = imu_status.sample_rate_hz;
  imu["active_samples"] = snapshot.timing.samples;
  imu["first_sample_us"] = snapshot.timing.first_us;
  imu["last_sample_us"] = snapshot.timing.last_us;
  imu["measured_rate_hz"] = snapshot.timing.rate_hz();
  imu["minimum_interval_us"] = snapshot.timing.minimum_interval_us;
  imu["maximum_interval_us"] = snapshot.timing.maximum_interval_us;
  imu["detected_i2c_address"] = imu_status.i2c_address;
  imu["who_am_i"] = imu_status.who_am_i;
  imu["sample_count"] = imu_status.sample_count;
  imu["last_error"] = imu_status.last_error;
  const auto &scan = snapshot.bus_scan;
  JsonObject bus_scan = imu["boot_scan"].to<JsonObject>();
  bus_scan["completed"] = scan.completed;
  bus_scan["error"] = scan.error;
  bus_scan["timeouts"] = scan.timeouts;
  bus_scan["attempted_addresses"] = scan.attempted;
  bus_scan["last_address"] = scan.last_address;
  bus_scan["attempts"] = scan.attempts;
  bus_scan["probe_timeout_ms"] = 100;
  bus_scan["sda_idle"] = scan.sda_idle;
  bus_scan["scl_idle"] = scan.scl_idle;
  bus_scan["sda_after"] = scan.sda_after;
  bus_scan["scl_after"] = scan.scl_after;
  JsonObject first = bus_scan["first_scan"].to<JsonObject>();
  first["sda_idle"] = scan.first_sda_idle;
  first["scl_idle"] = scan.first_scl_idle;
  first["error"] = scan.first_error;
  first["address_count"] = scan.first_count;
  JsonArray addresses = bus_scan["addresses"].to<JsonArray>();
  for (size_t i = 0; i < scan.count; ++i) addresses.add(scan.addresses[i]);
  JsonObject identities = bus_scan["register_0x75"].to<JsonObject>();
  const char *keys[] = {"0x68", "0x69", "0x74"};
  for (size_t i = 0; i < 3; ++i) {
    if (scan.identity_register_75[i] >= 0)
      identities[keys[i]] = scan.identity_register_75[i];
    else identities[keys[i]] = nullptr;
  }
}
template<class Log> void print_mpu6050_diagnostics(Log log, const Mpu6050Status &imu) {
  log(
           "imu initialized=%s sampling=%s address=0x%02x WHO_AM_I=0x%02x "
           "rate=%u Hz samples=%u latest_valid=%s pose_valid=%s error=%s",
           imu.initialized ? "yes" : "no", imu.sampling ? "yes" : "no",
           imu.i2c_address, imu.who_am_i,
           static_cast<unsigned>(imu.sample_rate_hz),
           static_cast<unsigned>(imu.sample_count),
           imu.latest.valid ? "yes" : "no", imu.pose.valid ? "yes" : "no",
           imu.last_error.empty() ? "(none)" : imu.last_error.c_str());
}

}
