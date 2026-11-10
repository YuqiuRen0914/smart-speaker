#include "lunaredu_lunaros/delivery/update.h"

#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/device_api/device_projection_json.h"
#include "lunaredu_lunaros/device_api/device_projection.h"
#include "device_api/projections/ota_payload.h"

namespace lunaredu::lunaros {
namespace {

int read_ota_bulk_chunk(uint8_t *buffer, size_t max_size, void *context) {
  DeviceBulkRequest *request = static_cast<DeviceBulkRequest *>(context);
  if (request == nullptr || request->read_chunk == nullptr) {
    return -1;
  }
  return request->read_chunk(buffer, max_size, request->context);
}

int status_code_for_ota_error(const OtaServiceStatus &status) {
  if (status.message == "empty_ota_stream") {
    return 400;
  }
  if (status.message == "ota_upload_in_progress" ||
      status.message == "ota_restart_pending") {
    return 409;
  }
  return 500;
}

DeviceOperationResult handle_ota_firmware_upload_bulk_payload(
    const DeviceBulkRequest &request, JsonObject payload) {
  DeviceOperationResult result;

  DeviceBulkRequest request_copy = request;
  OtaFirmwareStream stream;
  stream.content_length = request.content_length;
  stream.context = &request_copy;
  stream.read_chunk = read_ota_bulk_chunk;

  if (!ota_service_install_stream(stream)) {
    fill_ota_status_payload(payload);
    const OtaServiceStatus status = ota_service_status_snapshot();
    result.ok = false;
    result.status_code = status_code_for_ota_error(status);
    result.error = status.message.empty() ? "ota_install_failed"
                                          : status.message;
    return result;
  }

  fill_ota_status_payload(payload);
  result.status_code = 200;
  return result;
}

DeviceOperationResult handle_ota_restart_request(JsonVariantConst,
                                                    JsonObject payload) {
  ota_service_request_restart();
  fill_ota_status_payload(payload);
  return device_operation_ack();
}

DeviceOperationResult handle_ota_firmware_pull_request(
    JsonVariantConst request, JsonObject payload) {
  const std::string url = device_operation_string(request["url"]);
  const std::string sha256 = device_operation_string(request["sha256"]);
  const size_t size =
      request["size"].is<size_t>() ? request["size"].as<size_t>() : 0;
  if (url.empty() || sha256.empty() || size == 0) {
    return device_operation_error("invalid_ota_pull_request", 400,
                                     "ota_pull_failed");
  }
  uint32_t job_id = 0;
  std::string error;
  if (!ota_service_queue_install_from_https(url, sha256, size, job_id,
                                             error)) {
    fill_ota_status_payload(payload);
    return device_operation_error(
        error.empty() ? "ota_pull_failed" : error.c_str(),
        error.rfind("invalid_", 0) == 0
            ? 400
            : (error == "ota_pull_in_progress" ||
                       error == "ota_upload_in_progress" ||
                       error == "ota_restart_pending"
                   ? 409
                   : 503),
        "ota_pull_failed");
  }
  fill_ota_status_payload(payload);
  payload["job_id"] = job_id;
  DeviceOperationResult result = device_operation_ack();
  result.status_code = 202;
  return result;
}

constexpr DeviceOperationDefinition kOtaOperations[] = {
    make_device_json_provider("status.get", fill_ota_status_payload),
    make_device_json_command("restart", handle_ota_restart_request),
    make_device_upload(
        "firmware.upload", handle_ota_firmware_upload_bulk_payload,
        device_operation_on_executor(kNetworkIoServiceExecutor)),
    make_device_json_command("firmware.pull", handle_ota_firmware_pull_request),
};

const DeviceModuleProjectionMetadata kOtaModule = {
    "ota",
    "ota",
};

const DeviceModuleProjection kOtaProjection = {
    "ota_service",
    &kOtaModule,
    kOtaOperations,
    sizeof(kOtaOperations) / sizeof(kOtaOperations[0]),
};

} // namespace

bool setup_update_projection(std::string &error) {
  return register_device_projection(
      {&ota_capability_descriptor(), &kOtaProjection, nullptr, false},
      &error);
}

} // namespace lunaredu::lunaros
