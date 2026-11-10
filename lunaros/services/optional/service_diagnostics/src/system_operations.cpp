#include "device_api/device_operation_builders.h"
#include "lunaros_diagnostics_projection.h"
#include "device_api/device_api_context.h"
#include "diagnostics/session.h"
#include "diagnostic_session_operations.h"
#include "diagnostics_payload.h"
#include "diagnostics/runtime_diagnostics.h"
#include "diagnostic_capture_operations.h"
#include "diagnostic_checks.h"
#include "diagnostic_inventory.h"
#include "official_tools_operations.h"
#include "heap_tools.h"
#include "gprof_tools.h"
#include "session.h"
#include "sdkconfig.h"
#include <cstring>
namespace lunaredu::lunaros {
#if CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS || CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
namespace {
void gprof_status(DeviceApiContext &,JsonObject output){output["configured"]=false;internal::gprof_tools::status(output);}
DeviceOperationResult gprof_start(DeviceApiContext &,JsonVariantConst request,JsonObject output){
 if(!request.is<JsonObjectConst>())return device_operation_error("gprof_invalid_request",400,"expected_object");
 for(auto field:request.as<JsonObjectConst>())if(field.key()!="duration_ms")return device_operation_error("gprof_invalid_request",400,"unknown_field");
 if(!request["duration_ms"].isNull()&&!request["duration_ms"].is<uint32_t>())return device_operation_error("gprof_invalid_duration",400,"expected_integer");
 std::string error;if(!diagnostics::start_gprof_trace(request["duration_ms"]|5000U,error))return device_operation_error(error.c_str(),409,"gprof_start_failed");
 internal::gprof_tools::status(output);return device_operation_ack();
}
DeviceOperationResult gprof_stop(DeviceApiContext &,JsonVariantConst,JsonObject output){std::string error;if(!internal::gprof_tools::stop(error))return device_operation_error(error.c_str(),503,"gprof_stop_failed");internal::gprof_tools::status(output);return device_operation_ack();}
DeviceOperationResult gprof_read(DeviceApiContext &,JsonObject output){std::string error;if(!internal::gprof_tools::read(output,error))return device_operation_error(error.c_str(),409,"gprof_read_failed");return device_operation_ack();}
void heap_status(DeviceApiContext &,JsonObject output){output["configured"]=false;internal::heap_tools::status(output);}
DeviceOperationResult heap_start(DeviceApiContext &,JsonVariantConst request,JsonObject output){
  if(!request.is<JsonObjectConst>())return device_operation_error("heap_trace_invalid_request",400,"expected_object");
  for(auto field:request.as<JsonObjectConst>())if(field.key()!="duration_ms" && field.key()!="mode")return device_operation_error("heap_trace_invalid_request",400,"unknown_field");
  if(!request["mode"].isNull()&&!request["mode"].is<const char *>())return device_operation_error("heap_trace_invalid_mode",400,"invalid_mode");
  const auto mode=request["mode"]|"leaks";
  if(std::strcmp(mode,"leaks") && std::strcmp(mode,"all"))return device_operation_error("heap_trace_invalid_mode",400,"invalid_mode");
  if(!request["duration_ms"].isNull() && !request["duration_ms"].is<uint32_t>())return device_operation_error("heap_trace_invalid_duration",400,"expected_integer");
  std::string error;
  if(!diagnostics::start_heap_trace(request["duration_ms"]|30000U,!std::strcmp(mode,"leaks"),error))return device_operation_error(error.c_str(),409,"heap_trace_start_failed");
  internal::heap_tools::status(output);
  return device_operation_ack();
}
DeviceOperationResult heap_stop(DeviceApiContext &,JsonVariantConst,JsonObject output){std::string error;if(!internal::heap_tools::stop(error))return device_operation_error(error.c_str(),503,"heap_trace_stop_failed");internal::heap_tools::status(output);return device_operation_ack();}
DeviceOperationResult heap_read(DeviceApiContext &,JsonObject output){std::string error;if(!internal::heap_tools::read(output,error))return device_operation_error(error.c_str(),409,"heap_trace_read_failed");return device_operation_ack();}
void fill_official_tools(DeviceApiContext &, JsonObject output) { internal::fill_official_tools_status(output); }
DeviceOperationResult start_official_tools(DeviceApiContext &, JsonVariantConst request, JsonObject output) {
  return internal::start_official_tools(request,output);
}
DeviceOperationResult stop_official_tools(DeviceApiContext &, JsonVariantConst request, JsonObject output) {
  return internal::stop_official_tools(request,output);
}
void fill_system_diagnostics(DeviceApiContext &device, JsonObject payload) {
  internal::fill_system_diagnostics_payload(device, payload);
}
void fill_cpu_monitor_status(DeviceApiContext &, JsonObject payload) {
  payload["supported"] = system::cpu_monitor_supported();
  payload["active"] = system::cpu_monitor_active();
  payload["main_work_profile"]=system::main_work_profiling_active();
  payload["worker_work_profile"]=system::worker_work_profiling_active();
  payload["remaining_ms"] = system::cpu_monitor_remaining_ms();
  payload["minimum_duration_ms"] = 20'000;
  payload["maximum_duration_ms"] = 900'000;
}
DeviceOperationResult start_cpu_monitor(DeviceApiContext &device,
                                        JsonVariantConst request,
                                        JsonObject response) {
  uint32_t duration_ms = 120'000;
  if (!request["duration_ms"].isNull()) {
    if (!request["duration_ms"].is<uint32_t>())
      return device_operation_error("cpu_monitor_invalid_duration", 400,
                                    "cpu_monitor_start_failed");
    duration_ms = request["duration_ms"].as<uint32_t>();
  }
  if((!request["main_work_profile"].isNull()&&!request["main_work_profile"].is<bool>())||
     (!request["worker_work_profile"].isNull()&&!request["worker_work_profile"].is<bool>()))
    return device_operation_error("cpu_monitor_invalid_detail",400,"cpu_monitor_start_failed");
  std::string error;
  if (!system::start_cpu_monitor(duration_ms, error))
    return device_operation_error(error.c_str(),
        error == "cpu_monitor_unsupported" ? 503 : 400,
        "cpu_monitor_start_failed");
  system::set_main_work_profiling(request["main_work_profile"].as<bool>());
  system::set_worker_work_profiling(request["worker_work_profile"].as<bool>());
  fill_cpu_monitor_status(device, response);
  return device_operation_ack();
}
DeviceOperationResult stop_cpu_monitor(DeviceApiContext &device,
                                       JsonVariantConst, JsonObject response) {
  system::stop_cpu_monitor();
  fill_cpu_monitor_status(device, response);
  return device_operation_ack();
}
constexpr const char *kCpuMonitorStartSchema =
    R"({"type":"object","properties":{"duration_ms":{"type":"integer","minimum":20000,"maximum":900000},"main_work_profile":{"type":"boolean"},"worker_work_profile":{"type":"boolean"}},"additionalProperties":false})";

constexpr DeviceOperationDefinition operations[]={
  device_operation_diagnostic_access(make_device_json_provider("diagnostics.gprof.status",gprof_status),DeviceDiagnosticAccess::Session),
  device_operation_diagnostic_access(make_device_json_command("diagnostics.gprof.start",gprof_start),DeviceDiagnosticAccess::Session),
  device_operation_diagnostic_access(make_device_json_command("diagnostics.gprof.stop",gprof_stop),DeviceDiagnosticAccess::Session),
  device_operation_diagnostic_access(make_device_json_query("diagnostics.gprof.read",gprof_read),DeviceDiagnosticAccess::Session),
  device_operation_diagnostic_access(device_operation_on_caller(make_device_json_provider("diagnostics.heap.status",heap_status)),DeviceDiagnosticAccess::Session),
  device_operation_diagnostic_access(device_operation_on_caller(make_device_json_command("diagnostics.heap.start",heap_start)),DeviceDiagnosticAccess::Session),
  device_operation_diagnostic_access(device_operation_on_caller(make_device_json_command("diagnostics.heap.stop",heap_stop)),DeviceDiagnosticAccess::Session),
  device_operation_diagnostic_access(device_operation_on_caller(make_device_json_query("diagnostics.heap.read",heap_read)),DeviceDiagnosticAccess::Session),
  device_operation_diagnostic_access(device_operation_on_caller(
      make_device_json_provider("diagnostics.tools.status", fill_official_tools)),DeviceDiagnosticAccess::Session),
  device_operation_diagnostic_access(device_operation_on_caller(
      device_operation_schemas(make_device_json_command("diagnostics.tools.start",start_official_tools),
      R"({"type":"object","properties":{"duration_ms":{"type":"integer","minimum":100,"maximum":900000}},"additionalProperties":false})",nullptr)),DeviceDiagnosticAccess::Session),
  device_operation_diagnostic_access(device_operation_on_caller(
      make_device_json_command("diagnostics.tools.stop",stop_official_tools)),DeviceDiagnosticAccess::Session),
  device_operation_diagnostic_access(device_operation_on_caller(
      make_device_json_provider("diagnostics.checks.list", internal::fill_diagnostic_checks)),
      DeviceDiagnosticAccess::Session),
  device_operation_diagnostic_access(device_operation_on_caller(internal::diagnostic_check_operation()),
      DeviceDiagnosticAccess::Session),
  device_operation_diagnostic_access(device_operation_on_caller(internal::diagnostic_activities_operation()),
      DeviceDiagnosticAccess::Session),
  device_operation_diagnostic_access(device_operation_on_caller(internal::diagnostic_inventory_operation()),
      DeviceDiagnosticAccess::Session),
  device_operation_diagnostic_access(device_operation_on_caller(
      make_device_json_provider("diagnostics.capture.status", internal::fill_diagnostic_capture_status)),
      DeviceDiagnosticAccess::Session),
  device_operation_diagnostic_access(device_operation_on_caller(device_operation_schemas(
      make_device_json_command("diagnostics.capture.set", internal::set_diagnostic_capture),
      internal::kDiagnosticCaptureSetSchema, nullptr)), DeviceDiagnosticAccess::Session),
  device_operation_diagnostic_access(device_operation_on_caller(
      internal::diagnostic_capture_read_operation()),
      DeviceDiagnosticAccess::Session),
  device_operation_diagnostic_access(device_operation_on_caller(
      make_device_json_provider("diagnostics.session.status", internal::fill_diagnostic_session_status)),
      DeviceDiagnosticAccess::Bootstrap),
  device_operation_diagnostic_access(device_operation_on_caller(
      make_device_json_command("diagnostics.session.open", internal::open_system_diagnostic_session)),
      DeviceDiagnosticAccess::Bootstrap),
  device_operation_diagnostic_access(device_operation_on_caller(
      make_device_json_command("diagnostics.session.close", internal::close_system_diagnostic_session)),
      DeviceDiagnosticAccess::Session),
  device_operation_diagnostic_access(
      make_device_json_provider("diagnostics.get", fill_system_diagnostics),
      DeviceDiagnosticAccess::Session),
  device_operation_diagnostic_access(
      make_device_json_provider("cpu_monitor.status", fill_cpu_monitor_status),
      DeviceDiagnosticAccess::Session),
  device_operation_diagnostic_access(
      device_operation_schemas(
          make_device_json_command("cpu_monitor.start", start_cpu_monitor),
          kCpuMonitorStartSchema, nullptr),
      DeviceDiagnosticAccess::Session),
  device_operation_diagnostic_access(
      make_device_json_command("cpu_monitor.stop", stop_cpu_monitor),
      DeviceDiagnosticAccess::Session),
};
}
#endif
void fill_diagnostics_capabilities(DeviceApiContext &device, JsonObject payload) {
  const auto has=[&](const char *id) { return device.projections() && device.projections()->find_operation("system",id); };
  const bool available=has("diagnostics.get");
  payload["diagnostics_available"]=available;
  payload["diagnostics_session_required"]=true;
  payload["diagnostics_on_demand"]=true;
  payload["diagnostic_capture_switch"]=has("diagnostics.capture.set");
  payload["diagnostic_capture_default_enabled"]=false;
  payload["diagnostic_inventory"]=has("diagnostics.capabilities.get");
  payload["diagnostic_operation_trace"]=has("diagnostics.capture.read");
  payload["diagnostic_activity_summaries"]=has("diagnostics.activities.get");
  payload["diagnostic_checks"]=has("diagnostics.checks.run");
  payload["cpu_monitor_supported"]=has("cpu_monitor.status") && system::cpu_monitor_supported();
}
DeviceOperationsView diagnostics_device_operations(){
#if CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS || CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
 initialize_diagnostic_session(); return {operations,std::size(operations)};
#else
 return {};
#endif
}
}
