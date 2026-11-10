#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <array>
#include <ArduinoJson.h>
#include "device_api/device_operation_result.h"

// JSON/HTTP operation contracts are a projection of the neutral device model.
// They do not own projection registration, transport tasks or device state.
namespace lunaredu::lunaros {
struct DeviceApiContext;

enum class DeviceOperationMethod : uint8_t {
  Get,
  Post,
  Put,
  Delete,
};

enum class DeviceOperationHandlerKind : uint8_t {
  JsonQuery,
  JsonCommand,
  Bulk,
  Upload,
  Media,
};


using DeviceJsonProvider = void (*)(DeviceApiContext &device, JsonObject payload);
using DeviceJsonResultProvider = DeviceOperationResult (*)(DeviceApiContext &device, JsonObject payload);
using DeviceJsonCommandHandler =
    DeviceOperationResult (*)(DeviceApiContext &device, JsonVariantConst request_payload,
                       JsonObject response_payload);

// Borrowed declaration data, dispatched on the operation's owner exactly like
// ordinary callbacks. The binding and its context must outlive the registry.
struct DeviceJsonBinding {
  DeviceOperationResult (*invoke)(void *context, JsonVariantConst request,
                                 JsonObject response) = nullptr;
  void *context = nullptr;
};

struct DeviceBulkRequest {
  DeviceOperationMethod method = DeviceOperationMethod::Get;
  JsonVariantConst payload;
  const uint8_t *body_data = nullptr;
  size_t body_size = 0;
  size_t content_length = 0;
  void *context = nullptr;
  bool (*has_arg)(const char *name, void *context) = nullptr;
  std::string (*arg)(const char *name, void *context) = nullptr;
  std::string (*header)(const char *name, void *context) = nullptr;
  int (*read_chunk)(uint8_t *buffer, size_t max_size, void *context) = nullptr;
  // Transport-verified authority, never derived from JSON or query parameters.
  bool administrator_authorized = false;
};

using DeviceBulkHandler =
    DeviceOperationResult (*)(DeviceApiContext &device, const DeviceBulkRequest &request,
                       JsonObject response_payload);

struct DeviceMediaRequest {
  std::string path;
  int fps = 0;
  int frames = 0;
};

struct DeviceMediaWriter {
  void *context = nullptr;
  bool (*send_binary)(int status_code, const char *content_type,
                      const uint8_t *data, size_t size,
                      void *context) = nullptr;
  bool (*begin_stream)(int status_code, const char *content_type,
                       void *context) = nullptr;
  bool (*send_chunk)(const uint8_t *data, size_t size,
                     void *context) = nullptr;
  void (*end_stream)(void *context) = nullptr;
};

using DeviceMediaHandler =
    DeviceOperationResult (*)(DeviceApiContext &device, const DeviceMediaRequest &request,
                       const DeviceMediaWriter &writer);

struct DeviceSignalBinding {
  const char *role = nullptr;
  const char *resource = nullptr;
  int16_t gpio = -1;
  bool active_low_known = false;
  bool active_low = false;
};
struct DeviceParameterObservation {
  const char *name = nullptr;
  const char *unit = nullptr;
  bool range_known = false;
  double minimum = 0, maximum = 0;
};
struct DeviceOperationDiagnosticFacts {
  std::array<DeviceSignalBinding, 8> signals{};
  size_t signal_count = 0;
  std::array<DeviceParameterObservation, 8> parameters{};
  size_t parameter_count = 0;
};
// Reads the provider's synchronized configuration only. No probing, allocation,
// driver start or business mutation. Strings must have firmware lifetime.
using DeviceOperationDiagnosticProvider = bool (*)(DeviceApiContext &, DeviceOperationDiagnosticFacts &);
struct DeviceOperationDiagnosticBinding {
  bool (*observe)(void *, DeviceOperationDiagnosticFacts &) = nullptr;
  void *context = nullptr;
};

struct DeviceOperationProjection {
  // Stable semantic key used to join this binding with DeviceOperationSpec.
  // All execution ownership and access policy remain in the model.
  DeviceOperationMethod method = DeviceOperationMethod::Get;
  DeviceOperationHandlerKind handler_kind = DeviceOperationHandlerKind::JsonQuery;
  DeviceJsonProvider json_provider = nullptr;
  DeviceJsonResultProvider json_result_provider = nullptr;
  DeviceJsonCommandHandler json_command_handler = nullptr;
  DeviceBulkHandler bulk_handler = nullptr;
  DeviceMediaHandler media_handler = nullptr;
  const DeviceJsonBinding *json_binding = nullptr;
  size_t maximum_request_bytes = 4096; // Ordinary control JSON; uploads opt into streaming.
  DeviceOperationDiagnosticProvider diagnostic_provider = nullptr;
  const DeviceOperationDiagnosticBinding *diagnostic_binding = nullptr;
  // Owner opt-in for nonprivileged declarative composition. This grants no
  // diagnostic session or authority; the composed-call gate checks the model.
  bool composable = false;
};


DeviceOperationResult device_operation_error(
    const char *error, uint16_t status_code,
    const char *fallback_error = "device_projection_request_failed",
    OperationCompletion completion = OperationCompletion::Unknown,
    OperationError category = OperationError::Internal);

DeviceOperationResult device_operation_ack(uint16_t status_code = 200,
    OperationCompletion completion = OperationCompletion::Completed);

std::string device_operation_string(JsonVariantConst value);

} // namespace lunaredu::lunaros
