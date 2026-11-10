#include "lunaredu_device_mpu6050_imu.h"

#include "lunaredu_lunaros/kernel/service_executor.h"
#include "mpu6050_local_api.h"

namespace lunaredu::device {
namespace {

enum class Mpu6050Operation : uint8_t {
  ReadStatus,
  StartSampling,
  StopSampling,
  SetSampleRate,
  FillTelemetryBatch,
};

struct Mpu6050Request {
  Mpu6050Operation operation = Mpu6050Operation::ReadStatus;
  Mpu6050Status *status = nullptr;
  JsonDocument *document = nullptr;
  uint32_t after_sequence = 0;
  uint16_t value = 0;
};

void execute_mpu6050(Mpu6050Request &request,
                     lunaredu::lunaros::ServiceOperationResult &result) {
  switch (request.operation) {
  case Mpu6050Operation::ReadStatus:
    break;
  case Mpu6050Operation::StartSampling:
    result.ok = mpu6050_start_sampling(result.error);
    break;
  case Mpu6050Operation::StopSampling:
    mpu6050_stop_sampling();
    break;
  case Mpu6050Operation::SetSampleRate:
    result.ok = mpu6050_set_sample_rate_hz(request.value, result.error);
    break;
  case Mpu6050Operation::FillTelemetryBatch:
    if (request.document == nullptr) {
      result.ok = false;
      result.error = "missing_response_document";
      return;
    }
    fill_mpu6050_telemetry_batch_payload(
        (*request.document)["payload"].to<JsonObject>(),
        request.after_sequence, request.value);
    return;
  }
  if (request.status != nullptr) {
    *request.status = mpu6050_status();
  }
}

bool invoke(Mpu6050Request &request, std::string &error) {
  return lunaredu::lunaros::invoke_service_operation(
      lunaredu::lunaros::kMainServiceExecutor, execute_mpu6050, request,
      error);
}

} // namespace

bool dispatch_mpu6050_status(Mpu6050Status *status, std::string &error) {
  Mpu6050Request request;
  request.status = status;
  return invoke(request, error);
}

bool dispatch_mpu6050_start_sampling(Mpu6050Status *status,
                                     std::string &error) {
  Mpu6050Request request;
  request.operation = Mpu6050Operation::StartSampling;
  request.status = status;
  return invoke(request, error);
}

bool dispatch_mpu6050_stop_sampling(Mpu6050Status *status,
                                    std::string &error) {
  Mpu6050Request request;
  request.operation = Mpu6050Operation::StopSampling;
  request.status = status;
  return invoke(request, error);
}

bool dispatch_mpu6050_set_sample_rate(uint16_t sample_rate_hz,
                                      Mpu6050Status *status,
                                      std::string &error) {
  Mpu6050Request request;
  request.operation = Mpu6050Operation::SetSampleRate;
  request.status = status;
  request.value = sample_rate_hz;
  return invoke(request, error);
}

bool dispatch_mpu6050_fill_telemetry_batch_payload(JsonDocument &response,
                                                   uint32_t after_seq,
                                                   uint16_t limit,
                                                   std::string &error) {
  Mpu6050Request request;
  request.operation = Mpu6050Operation::FillTelemetryBatch;
  request.document = &response;
  request.after_sequence = after_seq;
  request.value = limit;
  return invoke(request, error);
}

} // namespace lunaredu::device
