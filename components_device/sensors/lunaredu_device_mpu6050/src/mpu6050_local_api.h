#pragma once

#include <ArduinoJson.h>

#include "device_api/device_operation_types.h"
#include "lunaredu_device_mpu6050_imu.h"

namespace lunaredu::lunaros {
class KernelRuntime;
}

namespace lunaredu::device {

using lunaredu::lunaros::DeviceOperationResult;

DeviceOperationResult fill_mpu6050_status_payload(
    lunaredu::lunaros::DeviceApiContext &device, JsonObject payload);
DeviceOperationResult handle_mpu6050_sampling_start(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload);
DeviceOperationResult handle_mpu6050_sampling_stop(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload);
DeviceOperationResult handle_mpu6050_config_set(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload);
DeviceOperationResult fill_mpu6050_telemetry_latest_payload(
    lunaredu::lunaros::DeviceApiContext &device, JsonObject payload);
DeviceOperationResult fill_mpu6050_telemetry_batch_payload(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload);

void fill_mpu6050_status_payload(JsonObject payload,
                                 const Mpu6050Status &status);
void fill_mpu6050_telemetry_payload(JsonObject payload,
                                    const Mpu6050Status &status);
void fill_mpu6050_telemetry_batch_payload(JsonObject payload,
                                          uint32_t after_seq,
                                          uint16_t limit);

bool dispatch_mpu6050_status(lunaredu::lunaros::KernelRuntime &runtime, Mpu6050Status *status, std::string &error);
bool dispatch_mpu6050_start_sampling(lunaredu::lunaros::KernelRuntime &runtime, Mpu6050Status *status,
                                     std::string &error);
bool dispatch_mpu6050_stop_sampling(lunaredu::lunaros::KernelRuntime &runtime, Mpu6050Status *status, std::string &error);
bool dispatch_mpu6050_set_sample_rate(lunaredu::lunaros::KernelRuntime &runtime, uint16_t sample_rate_hz,
                                      Mpu6050Status *status,
                                      std::string &error);
bool dispatch_mpu6050_fill_telemetry_batch_payload(lunaredu::lunaros::KernelRuntime &runtime, JsonDocument &response,
                                                   uint32_t after_seq,
                                                   uint16_t limit,
                                                   std::string &error);

} // namespace lunaredu::device
