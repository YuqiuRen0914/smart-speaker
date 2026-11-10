#include "lunaros_ota_projection.h"
#include "device_api/device_operation_builders.h"
#include "lunaros_ota_lifecycle.h"
#include "device_api/projection_composition.h"
#include "device_api/device_api_context.h"
#include "lunaredu_ota.h"

#include <esp_log.h>
#include <sdkconfig.h>
#include "device_api/device_projection.h"
#include "ota_payload.h"

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
  if (status.message=="ota_product_quiescence_timeout" ||
      status.message.rfind("ota_product_quiescence_timeout:",0)==0) return 409;
  if (status.message == "empty_ota_stream") {
    return 400;
  }
  if (status.message == "ota_upload_in_progress" ||
      status.message == "ota_restart_pending" || status.message == "ota_busy" ||
      status.message == "ota_recovery_required" ||
      status.message == "ota_boot_requires_application" ||
      status.message == "ota_application_image_required" ||
      status.message == "ota_boot_project_required" ||
      status.message == "recovery_handoff_required" || status.message == "recovery_target_unavailable") {
    return 409;
  }
  return 500;
}

DeviceOperationResult handle_ota_firmware_upload_bulk_payload([[maybe_unused]] DeviceApiContext &device, const DeviceBulkRequest &request, JsonObject payload) {
  DeviceOperationResult result;

  DeviceBulkRequest request_copy = request;
  OtaFirmwareStream stream;
  stream.content_length = request.content_length;
  stream.context = &request_copy;
  stream.read_chunk = read_ota_bulk_chunk;
  stream.maintenance_authorized = request.administrator_authorized;

  std::string error;
  if (!ota_service_install_stream(stream, error)) {
    fill_ota_status_payload(device, payload);
    auto status = ota_service_status_snapshot();
    status.message = error;
    result.ok = false;
    result.status_code = status_code_for_ota_error(status);
    result.error = error.empty() ? "ota_install_failed" : error;
    return result;
  }

  fill_ota_status_payload(device, payload);
  result.status_code = 200;
  return result;
}

DeviceOperationResult handle_ota_restart_request([[maybe_unused]] DeviceApiContext &device, JsonVariantConst,
                                                    JsonObject payload) {
  std::string error;
  if (!ota_service_try_request_restart(error))
    return device_operation_error(error.c_str(), 409, "restart_not_admitted");
  fill_ota_status_payload(device, payload);
  return device_operation_ack();
}

DeviceOperationResult handle_recovery_enter(DeviceApiContext &device, JsonVariantConst,
                                           JsonObject payload) {
  std::string error;
  if (!ota_service_request_recovery(error))
    return device_operation_error(error.c_str(), 409, "recovery_enter_failed");
  fill_ota_status_payload(device, payload);
  return device_operation_ack();
}
DeviceOperationResult handle_boot_upload(DeviceApiContext &device, const DeviceBulkRequest &request,
                                         JsonObject payload) {
  DeviceBulkRequest copy = request;
  OtaFirmwareStream stream;
  stream.content_length = request.content_length;
  stream.context = &copy;
  stream.read_chunk = read_ota_bulk_chunk;
  stream.maintenance_authorized = request.administrator_authorized;
  stream.bootstrap_update = true;
  std::string error;
  if (!ota_service_install_stream(stream,error))
    return device_operation_error(error.c_str(),409,"boot_update_failed");
  fill_ota_status_payload(device,payload);
  return device_operation_ack();
}
DeviceOperationResult handle_ota_firmware_pull_request([[maybe_unused]] DeviceApiContext &device, JsonVariantConst request, JsonObject payload) {
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
    fill_ota_status_payload(device, payload);
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
  fill_ota_status_payload(device, payload);
  payload["job_id"] = job_id;
  DeviceOperationResult result = device_operation_ack();
  result.status_code = 202;
  return result;
}

constexpr DeviceOperationDefinition kOtaOperations[] = {
    device_operation_remote(make_device_json_provider("status.get", fill_ota_status_payload), DeviceRemoteAccess::Query),
    make_device_json_command("restart", handle_ota_restart_request),
    // Preparation waits for product owners including main; keep that owner free.
    make_device_json_command("recovery.enter", handle_recovery_enter,
        device_operation_on_executor(kNetworkIoServiceExecutor)),
    make_device_upload("boot.upload", handle_boot_upload,
        device_operation_on_executor(kNetworkIoServiceExecutor)),
    make_device_upload(
        "firmware.upload", handle_ota_firmware_upload_bulk_payload,
        device_operation_on_executor(kNetworkIoServiceExecutor)),
    make_device_json_command("firmware.pull", handle_ota_firmware_pull_request,
        device_operation_on_executor(kNetworkIoServiceExecutor)),
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

static bool install_update_projection(DeviceProjectionRegistry &registry, std::string &error) {
  return registry.register_projection(
      {&ota_capability_descriptor(), &kOtaProjection, nullptr, false},
      &error);
}

const StandardProjectionContribution standard_projection::update{"update", install_update_projection};

} // namespace lunaredu::lunaros
