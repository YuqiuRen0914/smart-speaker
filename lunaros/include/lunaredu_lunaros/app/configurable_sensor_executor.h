#pragma once

#include <cstdint>
#include <string>

#include "lunaredu_lunaros/kernel/service_executor.h"

namespace lunaredu::device::execution {

enum class ConfigurableSensorOperation : uint8_t {
  ReadStatus,
  ReadSample,
  UpdateConfig,
};

template <typename Status, typename ConfigUpdate>
struct ConfigurableSensorRequest {
  ConfigurableSensorOperation operation =
      ConfigurableSensorOperation::ReadStatus;
  ConfigUpdate config_update{};
  Status *status = nullptr;
  const char *sample_error = "sensor_sample_failed";
};

template <typename Status, typename ConfigUpdate, Status (*ReadStatus)(),
          bool (*ReadSample)(),
          bool (*ApplyConfig)(const ConfigUpdate &, std::string &)>
void execute_configurable_sensor(
    ConfigurableSensorRequest<Status, ConfigUpdate> &request,
    lunaredu::lunaros::ServiceOperationResult &result) {
  switch (request.operation) {
  case ConfigurableSensorOperation::ReadStatus:
    break;
  case ConfigurableSensorOperation::ReadSample:
    if (!ReadSample()) {
      result.ok = false;
      result.error = ReadStatus().last_error;
      if (result.error.empty()) {
        result.error = request.sample_error;
      }
    }
    break;
  case ConfigurableSensorOperation::UpdateConfig:
    result.ok = ApplyConfig(request.config_update, result.error);
    break;
  }
  if (request.status != nullptr) {
    *request.status = ReadStatus();
  }
}

template <typename Status, typename ConfigUpdate, Status (*ReadStatus)(),
          bool (*ReadSample)(),
          bool (*ApplyConfig)(const ConfigUpdate &, std::string &)>
bool invoke_configurable_sensor(
    ConfigurableSensorOperation operation, const ConfigUpdate *config_update,
    Status *status, const char *sample_error, std::string &error) {
  ConfigurableSensorRequest<Status, ConfigUpdate> request;
  request.operation = operation;
  if (config_update != nullptr) {
    request.config_update = *config_update;
  }
  request.status = status;
  request.sample_error = sample_error;
  return lunaredu::lunaros::invoke_service_operation(
      lunaredu::lunaros::kMainServiceExecutor,
      execute_configurable_sensor<Status, ConfigUpdate, ReadStatus, ReadSample,
                                  ApplyConfig>,
      request, error);
}

} // namespace lunaredu::device::execution
