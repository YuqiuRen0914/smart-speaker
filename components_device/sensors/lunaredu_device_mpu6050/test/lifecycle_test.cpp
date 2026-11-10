#include "lunaredu_device_mpu6050_imu.h"
#include "mpu6050_instance_internal.h"
#include "i2c_bus.h"
#include "mpu6050.h"
#include <cassert>
#include <cstdio>
using namespace lunaredu::device;
int main() {
  Mpu6050ModuleConfig config;
  config.use_bsp_i2c = false;
  auto *instance = mpu6050_create_instance();
  assert(setup_mpu6050_instance(instance, config));
  assert(mpu6050_instance_read_now(instance));
  const auto telemetry = mpu6050_instance_telemetry(instance, 0, 16);
  assert(telemetry.status.latest.valid && telemetry.count == 1 && telemetry.samples[0].seq == 1);
  fail_bus_delete = true;
  assert(!mpu6050_instance_deinit(instance) && live_buses == 1 && live_sensors == 0);
  const int creates = bus_creates;
  assert(!setup_mpu6050_instance(instance, config) && bus_creates == creates);
  assert(!mpu6050_destroy_instance(instance));
  fail_bus_delete = false;
  assert(mpu6050_instance_deinit(instance) && live_buses == 0);
  assert(setup_mpu6050_instance(instance, config));
  fail_sensor_delete = true;
  assert(!mpu6050_instance_deinit(instance) && live_buses == 1 && live_sensors == 1);
  fail_sensor_delete = false;
  remaining_devices = 1;
  assert(!mpu6050_instance_deinit(instance) && live_buses == 1);
  remaining_devices = 0;
  assert(mpu6050_instance_deinit(instance) && live_buses == 0);
  fail_probe_delete = true;
  assert(!setup_mpu6050_instance(instance, config) && live_probes == 1 && live_buses == 1);
  fail_probe_delete = false;
  assert(mpu6050_instance_deinit(instance) && live_probes == 0 && live_buses == 0);
  int borrowed;
  config.external_i2c_bus = &borrowed;
  assert(setup_mpu6050_instance(instance, config) && live_buses == 0);
  assert(mpu6050_destroy_instance(instance) && live_sensors == 0 && live_probes == 0);
  std::puts("MPU6050 shared engine: telemetry, borrowed bus and retry after sensor/probe/bus release failures passed");
}
