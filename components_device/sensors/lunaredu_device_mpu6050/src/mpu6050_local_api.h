#pragma once

#include <ArduinoJson.h>

#include "lunaredu_lunaros/device_api/device_projection.h"
#include "lunaredu_device_mpu6050_imu.h"

namespace lunaredu::device {

using lunaredu::lunaros::DeviceOperationResult;

DeviceOperationResult fill_mpu6050_status_payload(
    JsonObject payload);
DeviceOperationResult handle_mpu6050_sampling_start(
    JsonVariantConst request_payload, JsonObject response_payload);
DeviceOperationResult handle_mpu6050_sampling_stop(
    JsonVariantConst request_payload, JsonObject response_payload);
DeviceOperationResult handle_mpu6050_config_set(
    JsonVariantConst request_payload, JsonObject response_payload);
DeviceOperationResult fill_mpu6050_telemetry_latest_payload(
    JsonObject payload);
DeviceOperationResult fill_mpu6050_telemetry_batch_payload(
    JsonVariantConst request_payload, JsonObject response_payload);

void fill_mpu6050_status_payload(JsonObject payload,
                                 const Mpu6050Status &status);
void fill_mpu6050_telemetry_payload(JsonObject payload,
                                    const Mpu6050Status &status);
void fill_mpu6050_telemetry_batch_payload(JsonObject payload,
                                          uint32_t after_seq,
                                          uint16_t limit);

bool dispatch_mpu6050_status(Mpu6050Status *status, std::string &error);
bool dispatch_mpu6050_start_sampling(Mpu6050Status *status,
                                     std::string &error);
bool dispatch_mpu6050_stop_sampling(Mpu6050Status *status, std::string &error);
bool dispatch_mpu6050_set_sample_rate(uint16_t sample_rate_hz,
                                      Mpu6050Status *status,
                                      std::string &error);
bool dispatch_mpu6050_fill_telemetry_batch_payload(JsonDocument &response,
                                                   uint32_t after_seq,
                                                   uint16_t limit,
                                                   std::string &error);

} // namespace lunaredu::device
