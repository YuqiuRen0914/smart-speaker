#include "lunaredu_lunaros/device_api/device_projection_json.h"

namespace lunaredu::lunaros {

DeviceOperationResult device_operation_error(const char *error, uint16_t status_code,
                               const char *fallback_error) {
  DeviceOperationResult result;
  result.ok = false;
  result.status_code = status_code;
  result.error = error != nullptr && error[0] != '\0'
                     ? error
                     : (fallback_error != nullptr ? fallback_error
                                                  : "device_projection_request_failed");
  return result;
}

DeviceOperationResult device_operation_ack(uint16_t status_code) {
  DeviceOperationResult result;
  result.status_code = status_code;
  return result;
}

std::string device_operation_string(JsonVariantConst value) {
  const char *raw = value.as<const char *>();
  return raw != nullptr ? std::string(raw) : std::string();
}

} // namespace lunaredu::lunaros
