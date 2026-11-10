#include "device_api/routes/device_api_route_support.h"

#include <cstring>

#include "device_api/http/http_request_api.h"

lunaredu::lunaros::DeviceOperationMethod current_device_operation_method() {
  using lunaredu::lunaros::DeviceOperationMethod;
  switch (current_request_method()) {
  case HTTP_POST:
    return DeviceOperationMethod::Post;
  case HTTP_PUT:
    return DeviceOperationMethod::Put;
  case HTTP_DELETE:
    return DeviceOperationMethod::Delete;
  default:
    return DeviceOperationMethod::Get;
  }
}

bool parse_device_module_path(std::string &module, std::string &operation) {
  std::string uri = current_request_uri();
  const std::string::size_type query = uri.find('?');
  if (query != std::string::npos) {
    uri.resize(query);
  }
  if (uri.rfind(kDeviceModuleRoutePrefix, 0) != 0) {
    return false;
  }
  const std::string tail = uri.substr(std::strlen(kDeviceModuleRoutePrefix));
  const std::string::size_type separator = tail.find('/');
  if (separator == std::string::npos || separator == 0 ||
      separator + 1 >= tail.size() ||
      tail.find('/', separator + 1) != std::string::npos) {
    return false;
  }
  module = tail.substr(0, separator);
  operation = tail.substr(separator + 1);
  return true;
}

void send_device_api_error(int status, const char *code,
                           const char *message) {
  JsonDocument response;
  response["apiVersion"] = kDeviceApiVersion;
  response["ok"] = false;
  JsonObject error = response["error"].to<JsonObject>();
  const char *resolved_code = code != nullptr ? code : "device_operation_error";
  error["code"] = resolved_code;
  error["message"] = message != nullptr ? message : resolved_code;
  error["retryable"] = status == 408 || status == 409 || status == 423 ||
                       status == 429 || status >= 500;
  serialize_and_send_json(status, response);
}

void send_device_operation_result(JsonDocument &response,
                                  const lunaredu::lunaros::DeviceOperationResult &result) {
  response["apiVersion"] = kDeviceApiVersion;
  response["ok"] = result.ok;
  if (!result.ok) {
    JsonObject error = response["error"].to<JsonObject>();
    const char *code =
        result.error.empty() ? "operation_failed" : result.error.c_str();
    error["code"] = code;
    error["message"] = code;
    const uint16_t status = result.status_code == 0 ? 500 : result.status_code;
    error["retryable"] = status == 408 || status == 409 || status == 423 ||
                         status == 429 || status >= 500;
  }
  const int status = result.status_code == 0 ? (result.ok ? 200 : 500)
                                              : result.status_code;
  serialize_and_send_json(status, response);
}
