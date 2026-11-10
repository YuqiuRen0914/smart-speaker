#include "device_api/device_operation_builders.h"
#include "model/device_model.h"
#include "mpu6050_instance_internal.h"
#include "mpu6050_local_api.h"
#include "bsp/lunaredu_bsp.h"
#include "device_api/device_projection.h"

namespace lunaredu::device {

using lunaredu::lunaros::DeviceCapabilityDescriptor;
using lunaredu::lunaros::DeviceModuleProjection;
using lunaredu::lunaros::DeviceModuleProjectionMetadata;
using lunaredu::lunaros::DeviceOperationDefinition;
using lunaredu::lunaros::DeviceOperationMethod;
using lunaredu::lunaros::DeviceProjectionBinding;
using lunaredu::lunaros::device_operation_method;
using lunaredu::lunaros::make_device_json_command;
using lunaredu::lunaros::make_device_json_query;

namespace {
bool describe_mpu6050(lunaredu::lunaros::DeviceApiContext &,
                     lunaredu::lunaros::DeviceOperationDiagnosticFacts &facts) {
  const auto config = mpu6050_instance_diagnostic_facts(default_mpu6050_instance());
  if (!config.initialized) return false;
  facts.signal_count = config.has_interrupt_pin ? 3 : 2;
  facts.signals[0] = {"sda", nullptr, config.bus_pins_known ? static_cast<int16_t>(config.sda_pin) : int16_t{-1}};
  facts.signals[1] = {"scl", nullptr, config.bus_pins_known ? static_cast<int16_t>(config.scl_pin) : int16_t{-1}};
  if (config.has_interrupt_pin) facts.signals[2] = {"interrupt", nullptr, config.interrupt_pin};
  facts.parameter_count = 4;
  facts.parameters[0] = {"sample_rate_hz", "Hz", true, 5, 1000};
  facts.parameters[1] = {"acceleration", "g", true, -2, 2};
  facts.parameters[2] = {"angular_velocity", "deg/s", true, -250, 250};
  facts.parameters[3] = {"temperature", "degC"};
  return true;
}

constexpr const char *kMpu6050PresenceTags[] = {
    "imu_mpu6050",
};

const DeviceCapabilityDescriptor kMpu6050Capability = []() {
  DeviceCapabilityDescriptor descriptor;
  descriptor.id = "mpu6050_module";
  descriptor.presence_capabilities = kMpu6050PresenceTags;
  descriptor.presence_capability_count =
      sizeof(kMpu6050PresenceTags) / sizeof(kMpu6050PresenceTags[0]);
  return descriptor;
}();

constexpr DeviceOperationDefinition kMpu6050Operations[] = {
    lunaredu::lunaros::device_operation_diagnostics(
        make_device_json_query("status.get", fill_mpu6050_status_payload), describe_mpu6050),
    make_device_json_command("sampling.start", handle_mpu6050_sampling_start),
    make_device_json_command("sampling.stop", handle_mpu6050_sampling_stop),
    make_device_json_command("config.set", handle_mpu6050_config_set),
    lunaredu::lunaros::device_operation_diagnostics(
        make_device_json_query("telemetry.latest.get", fill_mpu6050_telemetry_latest_payload), describe_mpu6050),
    make_device_json_command("telemetry.batch.get", fill_mpu6050_telemetry_batch_payload, device_operation_method(DeviceOperationMethod::Get)),
};

const DeviceModuleProjectionMetadata kMpu6050LocalApiMetadata = {
    "mpu6050",
    "mpu6050",
};

const DeviceModuleProjection kMpu6050LocalApi = {
    "mpu6050_module",
    &kMpu6050LocalApiMetadata,
    kMpu6050Operations,
    sizeof(kMpu6050Operations) / sizeof(kMpu6050Operations[0]),
};
DeviceProjectionBinding g_projection;

} // namespace

const DeviceCapabilityDescriptor &mpu6050_capability_descriptor() { return kMpu6050Capability; }

bool setup_mpu6050(const Mpu6050ModuleConfig &config) {
  return setup_mpu6050_with_bus(config, nullptr);
}

bool setup_mpu6050_with_bus(const Mpu6050ModuleConfig &config, i2c_master_bus_handle_t bus) {
  g_projection.deactivate(kMpu6050Capability.id);
  if (!mpu6050_instance_deinit(default_mpu6050_instance())) return false;
  auto resolved = config;
  if (bus) resolved.external_i2c_bus = bus;
  if (!resolved.external_i2c_bus && config.use_bsp_i2c) {
    if (!bsp_i2c_ready()) return false;
    resolved.external_i2c_bus = bsp_i2c_master_bus_handle();
    if (!resolved.external_i2c_bus) return false;
  }
  if (!setup_mpu6050_instance(default_mpu6050_instance(), resolved)) return false;
  // Some products initialize this module after their HTTP Device API surface is
  // already live and expose MPU telemetry through a product-specific endpoint.
  // Keep the sensor running even if the optional standalone projection cannot
  // be registered at that point.
  (void)g_projection.activate(kMpu6050Capability);
  return true;
}

bool mpu6050_deinit() {
  g_projection.deactivate(kMpu6050Capability.id);
  return mpu6050_instance_deinit(default_mpu6050_instance());
}
void tick_mpu6050() { tick_mpu6050_instance(default_mpu6050_instance()); }
bool mpu6050_read_now() { return mpu6050_instance_read_now(default_mpu6050_instance()); }
bool mpu6050_available() { return mpu6050_instance_available(default_mpu6050_instance()); }
Mpu6050Status mpu6050_status() { return mpu6050_instance_status(default_mpu6050_instance()); }
Mpu6050Pose mpu6050_pose() { return mpu6050_instance_pose(default_mpu6050_instance()); }
bool mpu6050_calibrate_gyro(uint16_t samples, std::string &error) {
  return mpu6050_instance_calibrate_gyro(default_mpu6050_instance(), samples, error);
}
void mpu6050_reset_pose() { mpu6050_instance_reset_pose(default_mpu6050_instance()); }
bool mpu6050_start_sampling(std::string &error) {
  return mpu6050_instance_start_sampling(default_mpu6050_instance(), error);
}
void mpu6050_stop_sampling() { mpu6050_instance_stop_sampling(default_mpu6050_instance()); }
bool mpu6050_set_sleep(bool sleep, std::string &error) {
  return mpu6050_instance_set_sleep(default_mpu6050_instance(), sleep, error);
}
bool mpu6050_set_sample_rate_hz(uint16_t rate, std::string &error) {
  return mpu6050_instance_set_sample_rate_hz(default_mpu6050_instance(), rate, error);
}

bool declare_mpu6050_projection(lunaredu::lunaros::DeviceProjectionRegistry &registry,
                                std::string &error) {
  return g_projection.declare(registry, {&kMpu6050Capability, &kMpu6050LocalApi}, error);
}

void fill_mpu6050_telemetry_batch_payload(JsonObject payload,
                                          uint32_t after_seq,
                                          uint16_t limit) {
  const auto telemetry = mpu6050_instance_telemetry(default_mpu6050_instance(), after_seq, limit);
  const auto &status = telemetry.status;
  payload["ok"] = status.initialized;
  payload["sampling"] = status.sampling;
  payload["sample_rate_hz"] = status.sample_rate_hz;
  payload["latest_seq"] = status.sample_count;
  payload["next_seq"] = status.sample_count + 1;
  JsonObject pose = payload["pose"].to<JsonObject>();
  pose["valid"] = status.pose.valid;
  pose["enabled"] = status.pose.enabled;
  pose["gyro_calibrated"] = status.pose.gyro_calibrated;
  pose["angle_x"] = status.pose.angle_x;
  pose["angle_y"] = status.pose.angle_y;
  pose["angle_z"] = status.pose.angle_z;
  pose["gyro_x_dps"] = status.pose.gyro_x_dps;
  pose["gyro_y_dps"] = status.pose.gyro_y_dps;
  pose["gyro_z_dps"] = status.pose.gyro_z_dps;

  payload["buffered"] = telemetry.buffered;
  JsonArray samples = payload["samples"].to<JsonArray>();
  for (uint16_t index = 0; index < telemetry.count; ++index) {
    const auto &sample = telemetry.samples[index];
    JsonObject item = samples.add<JsonObject>();
    item["seq"] = sample.seq;
    item["t_ms"] = sample.t_ms;
    item["ax"] = sample.accel_g_x;
    item["ay"] = sample.accel_g_y;
    item["az"] = sample.accel_g_z;
    item["gx"] = sample.gyro_dps_x;
    item["gy"] = sample.gyro_dps_y;
    item["gz"] = sample.gyro_dps_z;
    item["temperature_c"] = sample.temperature_c;
  }
}

} // namespace lunaredu::device
