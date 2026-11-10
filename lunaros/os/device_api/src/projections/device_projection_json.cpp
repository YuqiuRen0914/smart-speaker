#include "device_api/device_operation_types.h"

namespace lunaredu::lunaros {

DeviceOperationResult device_operation_error(const char *error, uint16_t status_code,
                               const char *fallback_error,
                               OperationCompletion completion, OperationError category) {
  DeviceOperationResult result;
  result.ok = false;
  result.status_code = status_code;
  result.completion=completion;result.category=category;
  result.error = error != nullptr && error[0] != '\0'
                     ? error
                     : (fallback_error != nullptr ? fallback_error
                                                  : "device_projection_request_failed");
  return result;
}

DeviceOperationResult device_operation_ack(uint16_t status_code, OperationCompletion completion) {
  DeviceOperationResult result;
  result.status_code = status_code;
  result.completion=completion;
  return result;
}

std::string device_operation_string(JsonVariantConst value) {
  const JsonString raw = value.as<JsonString>();
  return raw.isNull() ? std::string() : std::string(raw.c_str(),raw.size());
}

} // namespace lunaredu::lunaros
