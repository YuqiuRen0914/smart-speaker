#include "kernel/service.h"
#include "lunaredu_operation_result.h"
#include "lunaros_usb.h"
#include "lunaros_usb_host.h"
#include "console_input.h"
#include "response_output.h"

#include <soc/soc_caps.h>
#include "kernel/service_executor.h"

#if SOC_USB_SERIAL_JTAG_SUPPORTED

#include <ArduinoJson.h>
#include <driver/usb_serial_jtag.h>
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
#include <driver/usb_serial_jtag_vfs.h>
#endif
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#if CONFIG_LUNAROS_USB_DEVICE_LOG_OUTPUT
#include "kernel/static_mutex.h"
#include <cstdarg>
#include <cstdio>
#endif

#include "device_api/device_api.h"
#include "device_api/device_projection.h"
#include "device_api/operation_feedback.h"
#include "device_api/device_command_json.h"
#include "device_api/operation_dispatch.h"
#include "device_api/device_api_context.h"
#include "lunaros_device_protocol.h"
#include "lunaredu_device_wire_envelope.h"
#include "lunaredu_device_profiles.h"
#include "lunaros_usb_frame_dispatcher.h"
#include "diagnostics/session.h"

namespace lunaredu::lunaros {
namespace {

DeviceFrameDecoder decoder;
bool usb_ready = false;
bool usb_host_owned = false;
DeviceProjectionRegistry *usb_registry=nullptr;
UsbFrameDispatcher frames;
UsbConsoleInput console_input;
UsbResponseOutput response_output;
UsbConsoleLineHandler console_line_handler = nullptr;
void *console_line_context = nullptr;
int64_t console_last_rx_us = 0;
#if CONFIG_LUNAROS_USB_DEVICE_LOG_OUTPUT
StaticMutex log_mutex;
bool log_enabled = false, log_installed = false;
vprintf_like_t previous_log = nullptr;
int device_log(const char *format, va_list args) {
  // Preserve any preceding sink. With ESP_CONSOLE_NONE the SDK's default sink
  // cannot touch USB/UART; later diagnostics capture can wrap this callback.
  va_list copy; va_copy(copy,args);
  const int result = previous_log ? previous_log(format,copy) : 0;
  va_end(copy);
  StaticLockGuard lock(log_mutex);
  if (!log_enabled) return result;
  char bytes[224];
  const int n = std::vsnprintf(bytes,sizeof(bytes),format,args);
  if (n > 0) (void)usb_serial_jtag_write_bytes(bytes,std::min(size_t(n),sizeof(bytes)-1),0);
  return result;
}
void enable_device_log(bool enabled) { StaticLockGuard lock(log_mutex); log_enabled=enabled; }
#else
void enable_device_log(bool) {}
#endif

struct QueryInvocation {
  DeviceApiContext *device;
  const DeviceOperationProjection *projection;
  JsonVariantConst input;
  JsonObject payload;
  DeviceOperationResult result;
};

void invoke_query(void *context, ServiceOperationResult &status) {
  auto &call = *static_cast<QueryInvocation *>(context);
  if (call.projection->json_binding != nullptr) {
    call.result = call.projection->json_binding->invoke(
        call.projection->json_binding->context, call.input,
        call.payload);
  } else if (call.projection->json_result_provider != nullptr) {
    call.result = call.projection->json_result_provider(*call.device, call.payload);
  } else if (call.projection->json_provider != nullptr) {
    call.projection->json_provider(*call.device, call.payload);
  } else {
    call.result = {false, 501, "json_provider_unavailable"};
  }
  status.work_result_known = true;
  status.work_ok = call.result.ok;
  status.work_error_code = call.result.error.c_str();
  status.ok = true;
}

void write_json(const DeviceFrame &request, JsonDocument &document) {
  if (!usb_ready || !usb_registry) return;
  DeviceFrame response;
  response.type = request.type | kDeviceFrameResponseFlag;
  response.request_id = request.request_id;
  // Retain the former 18 KiB response bound, even though the common encoder
  // packs chunks more efficiently. Do not serialize a second oversized copy.
  if (measureJson(document) > protocol::profiles::fragment_max_response_bytes)
    response.payload = "{\"ok\":false,\"result\":{},\"error\":{\"code\":\"usb_response_too_large\"}}";
  else serializeJson(document, response.payload);
  if (!response_output.begin(std::move(response), esp_timer_get_time()))
    report_device_host_connection(usb_registry->device(), DeviceHostTransport::Usb, DeviceHostConnectionState::Error);
}

void send_error(const DeviceFrame &request, const char *error) {
  JsonDocument response;
  protocol::write_operation_error(response.to<JsonObject>(),error);
  response["result"].to<JsonObject>();
  write_json(request, response);
}


void handle_frame(DeviceProjectionRegistry &registry,
                  const DeviceFrame &request) {
  report_device_host_connection(usb_registry->device(), DeviceHostTransport::Usb, DeviceHostConnectionState::Connected);
  if (request.type >= static_cast<uint8_t>(DeviceFrameType::GetInfo) &&
      request.type <= static_cast<uint8_t>(DeviceFrameType::GetBinding)) {
    DeviceFrame reply;
    frames.dispatch(registry.runtime(), request, reply);
    JsonDocument document;
    if (deserializeJson(document, reply.payload)) {
      send_error(request, "device_function_response_invalid");
      return;
    }
    reply.type &= ~kDeviceFrameResponseFlag;
    write_json(reply, document);
    return;
  }
  if (request.type != static_cast<uint8_t>(DeviceFrameType::DeviceCommand)) {
    send_error(request, "usb_readonly_command_required");
    return;
  }
  JsonDocument input;
  if (deserializeJson(input, request.payload) != DeserializationError::Ok ||
      !input["module_id"].is<const char *>() || !input["operation_id"].is<const char *>() || !input["request"].is<JsonObject>()) {
    send_error(request, "invalid_command_payload");
    return;
  }
  const std::string module_id = input["module_id"].as<const char *>();
  for (const auto field : input.as<JsonObjectConst>()) {
    const std::string_view key = field.key().c_str();
    if (key != "module_id" && key != "operation_id" && key != "request" && key != "authority") {
      send_error(request, "invalid_command_payload"); return;
    }
  }
  if (!input["authority"].isNull() &&
      (!input["authority"].is<JsonObject>() || input["authority"].size() != 1 ||
       !input["authority"]["debug_token"].is<const char *>() ||
       std::strlen(input["authority"]["debug_token"].as<const char *>()) > 256)) {
    send_error(request, "invalid_local_authority"); return;
  }
  const std::string operation_id = input["operation_id"].as<const char *>();
  if (!protocol::operation_id(module_id) || !protocol::operation_id(operation_id)) {send_error(request,"invalid_operation_target");return;}
  const std::string name = module_id + "." + operation_id;
  const auto arguments = input["request"].as<JsonVariantConst>();
  const auto *extension=device_command_extension();
  JsonDocument response;
  response["ok"] = true;
  response["module_id"] = module_id;
  response["operation_id"] = operation_id;
  JsonObject result = response["result"].to<JsonObject>();
  if (name == "usb.operations.get") {
    result["transport"] = "usb_serial_jtag";
    result["protocol"] = "LP/1";
    result["commands"] =
        "usb.operations.get, usb.info.get, usb.status.get, diagnostics.session.open, "
        "diagnostics.session.close, module.operation";
    if(extension)result["extension_commands"]=extension->commands;
    result["access"] = extension?"read_only_plus_local_extension":"read_only";
  } else if (name == "usb.info.get") {
    result["transport"] = "usb_serial_jtag";
    result["protocol"] = "LP/1";
    result["device_api_sealed"] = registry.sealed();
  } else if (name == "usb.status.get") {
    result["uptime_ms"] = static_cast<uint64_t>(esp_timer_get_time() / 1000);
    result["free_heap_bytes"] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    result["device_api_sealed"] = registry.sealed();
  } else if (name == "diagnostics.session.open") {
    const char *code = arguments["code"].is<const char *>()
                           ? arguments["code"].as<const char *>() : "";
    std::string token;
    std::string error;
    if (!open_diagnostic_session(code, token, error)) {
      send_error(request, error.c_str());
      return;
    }
    result["token"] = token;
  } else if (name == "diagnostics.session.close") {
    const char *token = input["authority"]["debug_token"].is<const char *>()
                            ? input["authority"]["debug_token"].as<const char *>() : "";
    if (!diagnostic_session_token_valid(token)) {
      send_error(request, "debug_session_required");
      return;
    }
    result["closed"] = close_diagnostic_session();
  } else if (extension && extension->accepts(name)) {
    if (!registry.context().capabilities.active(extension->capability)) {
      send_error(request, "capability_unavailable");
      return;
    }
    std::string error;
    if (!invoke_device_command_json(*extension,name, arguments, result, error))
      error = "unknown_operation";
    if (!error.empty()) {
      send_error(request, error.c_str());
      return;
    }
  } else {
    const size_t separator = name.find('.');
    if (separator == std::string::npos || separator == 0 ||
        separator + 1 == name.size()) {
      send_error(request, "unknown_operation");
      return;
    }
    const std::string module = module_id;
    const std::string operation = operation_id;
    const DeviceModuleSpec *model =
        registry.context().models.find_module(module.c_str());
    const DeviceOperationSpec *spec =
        registry.context().models.find_operation(module.c_str(), operation.c_str());
    const DeviceOperationProjection *projection =
        registry.find_operation(module.c_str(), operation.c_str());
    if (model == nullptr || spec == nullptr || projection == nullptr) {
      send_error(request, "operation_not_found");
      return;
    }
    if (spec->mutating || projection->method != DeviceOperationMethod::Get ||
        projection->handler_kind != DeviceOperationHandlerKind::JsonQuery) {
      send_error(request, "usb_readonly_operation_required");
      return;
    }
    const char *token = input["authority"]["debug_token"].is<const char *>()
                            ? input["authority"]["debug_token"].as<const char *>() : "";
    if (spec->diagnostic_access == DeviceDiagnosticAccess::Session &&
        !diagnostic_session_token_valid(token)) {
      send_error(request, "debug_session_required");
      return;
    }
    if (!registry.context().capabilities.active(model->capability_id)) {
      send_error(request, "capability_unavailable");
      return;
    }
    QueryInvocation call{&registry.device(), projection, arguments, result, {}};
    auto correlation = begin_device_operation_feedback(registry.device());
    std::string dispatch_error;
    const bool dispatched = dispatch_device_operation(registry.runtime(), *spec,
                                              invoke_query, &call,
                                              dispatch_error, module.c_str(), &correlation);
    if (!dispatched) {
      response.clear();
      response["ok"] = false;
      response["error"]["code"] = "operation_owner_unavailable";
      response["result"].to<JsonObject>();
      fill_device_operation_correlation(response.as<JsonObject>(), correlation);
      write_json(request, response);
      return;
    }
    fill_device_operation_correlation(response.as<JsonObject>(), correlation);
    if (!call.result.ok) {
      response["ok"] = false;
      response["error"]["code"] = call.result.error;
    }
  }
  write_json(request, response);
}

}  // namespace

void set_usb_console_line_handler(UsbConsoleLineHandler handler, void *context) {
  console_line_handler = handler;
  console_line_context = context;
  console_input.reset();
}

namespace {
ServiceStartResult start_usb(void *context){
 if(usb_host_owned)return {ServiceStartDisposition::Deferred,"usb_host_owned"};
 auto *registry=static_cast<DeviceProjectionRegistry *>(context);
 if(!registry || !registry->sealed())return {ServiceStartDisposition::Failed,"usb_projection_not_sealed"};
 if(usb_registry && usb_registry!=registry)return {ServiceStartDisposition::Failed,"usb_owner_conflict"};
 if(usb_registry==registry && usb_ready)return {ServiceStartDisposition::Ready,"usb_ready"};
    if (usb_serial_jtag_is_driver_installed()) {
      usb_ready = true;
    } else {
      usb_serial_jtag_driver_config_t config = {};
      config.tx_buffer_size = 4096;
      config.rx_buffer_size = 4096;
      usb_ready = usb_serial_jtag_driver_install(&config) == ESP_OK;
    }
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
    if (usb_ready) usb_serial_jtag_vfs_use_driver();
#endif

 if(!usb_ready)return {ServiceStartDisposition::Failed,"usb_driver_unavailable"};
 // A borrowed console driver retains RX while admission is closed. Discard
 // those bytes before exposing a new session, with bounded main-owner work.
 uint8_t discarded[128];
 for(unsigned attempt=0;attempt<64;++attempt){
  if(usb_serial_jtag_read_bytes(discarded,sizeof(discarded),0)<=0)break;
  if(attempt==63){usb_ready=false;return {ServiceStartDisposition::Failed,"usb_rx_busy"};}
 }
 usb_registry=registry;decoder=DeviceFrameDecoder{};response_output.reset();console_input.reset();frames.reset();console_last_rx_us=0;
 enable_device_log(true);
 return {ServiceStartDisposition::Ready,"usb_ready"};
}
bool stop_usb(void *context,std::string &error){
 if(usb_host_owned){error="usb_host_owned";return false;}
 if(usb_registry && usb_registry!=context){error="usb_owner_conflict";return false;}
 enable_device_log(false);
 if(context) report_device_host_connection(static_cast<DeviceProjectionRegistry *>(context)->device(), DeviceHostTransport::Usb, DeviceHostConnectionState::Disconnected);
 usb_registry=nullptr;usb_ready=false;decoder=DeviceFrameDecoder{};response_output.reset();console_input.reset();frames.reset();console_last_rx_us=0;
 error.clear();return true;
}
}
bool suspend_usb_device_for_host(DeviceProjectionRegistry &registry,std::string &error){
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED || CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG
 (void)registry;error="usb_host_console_conflict";return false;
#else
 if(usb_host_owned){error="usb_host_owned";return false;}
 if(usb_registry && usb_registry!=&registry){error="usb_owner_conflict";return false;}
 if(!stop_usb(&registry,error))return false;
 if(usb_serial_jtag_is_driver_installed() && usb_serial_jtag_driver_uninstall()!=ESP_OK){
   (void)start_usb(&registry);error="usb_device_driver_uninstall_failed";return false;
 }
 usb_host_owned=true;error.clear();return true;
#endif
}
bool resume_usb_device_after_host(DeviceProjectionRegistry &registry,std::string &error){
 usb_host_owned=false;
 const auto result=start_usb(&registry);
 if(result.disposition!=ServiceStartDisposition::Ready){usb_host_owned=true;error=result.detail;return false;}
 error.clear();return true;
}
ServiceNode usb_service_node(DeviceProjectionRegistry &registry){
#if CONFIG_LUNAROS_USB_DEVICE_LOG_OUTPUT
 // Boot declaration installs the sink before service-owned diagnostic capture.
 if(!log_installed){previous_log=esp_log_set_vprintf(device_log);log_installed=true;}
#endif
 static constexpr ServiceDependency dependencies[]={{kKernelReadyServiceId,true}};
 return {{kUsbServiceId,"USB Device API",kMainServiceExecutor,ServiceCriticality::Optional,dependencies,1},start_usb,stop_usb,&registry};
}
void poll_usb_device_api(DeviceProjectionRegistry &registry) {
 if(usb_registry!=&registry || !registry.sealed())return;
  if (!usb_ready) return;
  const auto pump = [&] {
    const auto result = response_output.poll(
        [](const uint8_t *data, size_t size) {
          return usb_serial_jtag_write_bytes(data, size, 0);
        }, [] { return esp_timer_get_time(); });
    if (result == UsbResponseProgress::Failed)
      report_device_host_connection(usb_registry->device(), DeviceHostTransport::Usb, DeviceHostConnectionState::Error);
  };
  if (response_output.pending()) { pump(); return; }
  const auto decode = [&](const uint8_t *data, size_t size, bool final) {
    std::vector<DeviceFrame> frames;
    decoder.append(data, size, frames);
    for (const auto &frame : frames) handle_frame(registry, frame);
    if (final) decoder.reset();
  };
  // Read only consumed bytes. Once a response is pending, the next request
  // stays in the borrowed driver's RX ring; no extra command FIFO or raw RX
  // cache can overwrite output or outlive the service owner.
  for (unsigned consumed = 0; consumed < 128; ++consumed) {
    uint8_t byte;
    if (usb_serial_jtag_read_bytes(&byte, 1, 0) != 1) break;
    const int64_t now = esp_timer_get_time();
    if (console_last_rx_us && now - console_last_rx_us > 2000000) {
      console_input.reset(); decoder=DeviceFrameDecoder{};
    }
    console_last_rx_us = now;
    console_input.append(&byte, 1, decode,
        [](const char *line) {
          if (console_line_handler) console_line_handler(line, console_line_context);
        });
    if (usb_registry != &registry || !usb_ready) return;
    if (response_output.pending()) { pump(); break; }
  }
}

}  // namespace lunaredu::lunaros

#else

namespace lunaredu::lunaros {
void set_usb_console_line_handler(UsbConsoleLineHandler, void *) {}
void poll_usb_device_api(DeviceProjectionRegistry &) {}
ServiceNode usb_service_node(DeviceProjectionRegistry &registry){
 static constexpr ServiceDependency dependencies[]={{kKernelReadyServiceId,true}};
 return {{kUsbServiceId,"USB Device API",kMainServiceExecutor,ServiceCriticality::Optional,dependencies,1},[](void *){return ServiceStartResult{ServiceStartDisposition::Degraded,"usb_not_supported"};},[](void *,std::string &e){e.clear();return true;},&registry};
}
}  // namespace lunaredu::lunaros

#endif
