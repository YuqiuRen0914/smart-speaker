#pragma once

#include <string>

#include <ArduinoJson.h>

#include "lunaredu_lunaros/device_api/device_projection.h"

inline constexpr const char *kDeviceApiVersion = "1";
inline constexpr const char *kDeviceModuleRoutePrefix = "/api/v1/modules/";

lunaredu::lunaros::DeviceOperationMethod current_device_operation_method();
bool parse_device_module_path(std::string &module, std::string &operation);
void send_device_api_error(int status, const char *code,
                           const char *message = nullptr);
void send_device_operation_result(JsonDocument &response,
                                  const lunaredu::lunaros::DeviceOperationResult &result);
