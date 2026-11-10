#include "mpu6050_local_api.h"

#include "device_api/device_operation_types.h"
#include "device_api/device_api_context.h"

namespace lunaredu::device {

using lunaredu::lunaros::DeviceOperationResult;
using lunaredu::lunaros::device_operation_ack;
using lunaredu::lunaros::device_operation_error;

namespace {

void fill_sample_payload(JsonObject payload, const Mpu6050Sample &sample) {
  payload["valid"] = sample.valid;
  payload["seq"] = sample.seq;
  payload["t_ms"] = sample.t_ms;
  payload["accel_raw_x"] = sample.accel_raw_x;
  payload["accel_raw_y"] = sample.accel_raw_y;
  payload["accel_raw_z"] = sample.accel_raw_z;
  payload["accel_g_x"] = sample.accel_g_x;
  payload["accel_g_y"] = sample.accel_g_y;
  payload["accel_g_z"] = sample.accel_g_z;
  payload["gyro_raw_x"] = sample.gyro_raw_x;
  payload["gyro_raw_y"] = sample.gyro_raw_y;
  payload["gyro_raw_z"] = sample.gyro_raw_z;
  payload["gyro_dps_x"] = sample.gyro_dps_x;
  payload["gyro_dps_y"] = sample.gyro_dps_y;
  payload["gyro_dps_z"] = sample.gyro_dps_z;
  payload["temperature_c"] = sample.temperature_c;
}

void fill_pose_payload(JsonObject payload, const Mpu6050Pose &pose) {
  payload["valid"] = pose.valid;
  payload["enabled"] = pose.enabled;
  payload["gyro_calibrated"] = pose.gyro_calibrated;
  payload["seq"] = pose.seq;
  payload["t_ms"] = pose.t_ms;
  payload["dt_s"] = pose.dt_s;
  payload["angle_x"] = pose.angle_x;
  payload["angle_y"] = pose.angle_y;
  payload["angle_z"] = pose.angle_z;
  payload["accel_angle_x"] = pose.accel_angle_x;
  payload["accel_angle_y"] = pose.accel_angle_y;
  payload["gyro_angle_x"] = pose.gyro_angle_x;
  payload["gyro_angle_y"] = pose.gyro_angle_y;
  payload["gyro_angle_z"] = pose.gyro_angle_z;
  payload["gyro_x_dps"] = pose.gyro_x_dps;
  payload["gyro_y_dps"] = pose.gyro_y_dps;
  payload["gyro_z_dps"] = pose.gyro_z_dps;
  payload["gyro_offset_x"] = pose.gyro_offset_x;
  payload["gyro_offset_y"] = pose.gyro_offset_y;
  payload["gyro_offset_z"] = pose.gyro_offset_z;
}

} // namespace

DeviceOperationResult fill_mpu6050_status_payload(
    lunaredu::lunaros::DeviceApiContext &device, JsonObject payload) {
  Mpu6050Status status;
  std::string error;
  if (!dispatch_mpu6050_status(device.runtime, &status, error)) {
    return device_operation_error(
        error.empty() ? "mpu6050_status_unavailable" : error.c_str(), 500,
        "mpu6050_payload_unavailable");
  }

  fill_mpu6050_status_payload(payload, status);
  return DeviceOperationResult();
}

DeviceOperationResult handle_mpu6050_sampling_start(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload) {
  (void)request_payload;

  Mpu6050Status status;
  std::string error;
  if (!dispatch_mpu6050_start_sampling(device.runtime, &status, error)) {
    return device_operation_error(
        error.empty() ? "mpu6050_sampling_start_failed" : error.c_str(), 400,
        "mpu6050_payload_unavailable");
  }

  fill_mpu6050_status_payload(response_payload, status);
  return device_operation_ack();
}

DeviceOperationResult handle_mpu6050_sampling_stop(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload) {
  (void)request_payload;

  Mpu6050Status status;
  std::string error;
  if (!dispatch_mpu6050_stop_sampling(device.runtime, &status, error)) {
    return device_operation_error(
        error.empty() ? "mpu6050_sampling_stop_failed" : error.c_str(), 500,
        "mpu6050_payload_unavailable");
  }

  fill_mpu6050_status_payload(response_payload, status);
  return device_operation_ack();
}

DeviceOperationResult handle_mpu6050_config_set(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload) {
  const uint16_t sample_rate_hz =
      static_cast<uint16_t>(request_payload["sample_rate_hz"] | 0);
  if (sample_rate_hz == 0) {
    return device_operation_error("missing_sample_rate_hz", 400,
                                     "mpu6050_payload_unavailable");
  }

  Mpu6050Status status;
  std::string error;
  if (!dispatch_mpu6050_set_sample_rate(device.runtime, sample_rate_hz, &status, error)) {
    return device_operation_error(
        error.empty() ? "mpu6050_config_set_failed" : error.c_str(), 400,
        "mpu6050_payload_unavailable");
  }

  fill_mpu6050_status_payload(response_payload, status);
  return device_operation_ack();
}

DeviceOperationResult fill_mpu6050_telemetry_latest_payload(
    lunaredu::lunaros::DeviceApiContext &device, JsonObject payload) {
  Mpu6050Status status;
  std::string error;
  if (!dispatch_mpu6050_status(device.runtime, &status, error)) {
    return device_operation_error(
        error.empty() ? "mpu6050_telemetry_unavailable" : error.c_str(), 500,
        "mpu6050_payload_unavailable");
  }

  fill_mpu6050_telemetry_payload(payload, status);
  return DeviceOperationResult();
}

DeviceOperationResult fill_mpu6050_telemetry_batch_payload(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload) {
  const uint32_t after_seq =
      static_cast<uint32_t>(request_payload["after_seq"] | 0);
  const uint16_t limit = static_cast<uint16_t>(request_payload["limit"] | 8);

  JsonDocument document;
  std::string error;
  if (!dispatch_mpu6050_fill_telemetry_batch_payload(device.runtime, document, after_seq, limit,
                                                     error)) {
    return device_operation_error(
        error.empty() ? "mpu6050_telemetry_unavailable" : error.c_str(), 500,
        "mpu6050_payload_unavailable");
  }

  response_payload.set(document["payload"]);
  return DeviceOperationResult();
}

void fill_mpu6050_status_payload(JsonObject payload,
                                 const Mpu6050Status &status) {
  payload["ok"] = status.ok;
  payload["initialized"] = status.initialized;
  payload["sampling"] = status.sampling;
  payload["sleeping"] = status.sleeping;
  payload["i2c_address"] = status.i2c_address;
  payload["who_am_i"] = status.who_am_i;
  payload["sample_rate_hz"] = status.sample_rate_hz;
  payload["sample_interval_ms"] = status.sample_interval_ms;
  payload["sample_count"] = status.sample_count;
  payload["last_sample_ms"] = status.last_sample_ms;
  payload["last_error"] = status.last_error;
  fill_sample_payload(payload["latest"].to<JsonObject>(), status.latest);
  fill_pose_payload(payload["pose"].to<JsonObject>(), status.pose);
}

void fill_mpu6050_telemetry_payload(JsonObject payload,
                                    const Mpu6050Status &status) {
  payload["ok"] = status.ok;
  payload["sampling"] = status.sampling;
  payload["sample_rate_hz"] = status.sample_rate_hz;

  JsonObject sample = payload["sample"].to<JsonObject>();
  sample["valid"] = status.latest.valid;
  sample["seq"] = status.latest.seq;
  sample["t_ms"] = status.latest.t_ms;
  sample["ax"] = status.latest.accel_g_x;
  sample["ay"] = status.latest.accel_g_y;
  sample["az"] = status.latest.accel_g_z;
  sample["gx"] = status.latest.gyro_dps_x;
  sample["gy"] = status.latest.gyro_dps_y;
  sample["gz"] = status.latest.gyro_dps_z;
  sample["temperature_c"] = status.latest.temperature_c;
  fill_pose_payload(payload["pose"].to<JsonObject>(), status.pose);
}

} // namespace lunaredu::device
