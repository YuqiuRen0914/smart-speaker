#include "device_api/character_project_projection.h"
#include "device_api/device_operation_builders.h"
#include "character/project.h"
namespace lunaredu::pet {
namespace {
using namespace lunaros;
DeviceOperationResult project_state(DeviceApiContext &,JsonObject out){character::restore();character::poll_fetch();character::fill_state(out);return device_operation_ack();}
DeviceOperationResult project_install(DeviceApiContext &,const DeviceBulkRequest &request,JsonObject out){
  const char *json=request.payload["project_json"].as<const char *>();
  const char *hash=request.payload["sha256"].as<const char *>();std::string error;
  if(!json||!hash)return device_operation_error("character_project_request_invalid",400);
  if(!character::install(json,hash,error))return device_operation_error(error.c_str(),409);
  character::fill_state(out);out["installed"]=true;return device_operation_ack();
}
DeviceOperationResult project_activate(DeviceApiContext &,JsonVariantConst request,JsonObject out){
  const char *revision=request["revision"].as<const char *>();std::string error;
  if(!revision)return device_operation_error("character_project_revision_required",400);
  if(!character::activate(revision,error))return device_operation_error(error.c_str(),409);
  character::fill_state(out);out["activated"]=true;return device_operation_ack();
}
DeviceOperationResult project_remove(DeviceApiContext &,JsonVariantConst,JsonObject out){
  std::string error;if(!character::remove(error))return device_operation_error(error.c_str(),409);
  character::fill_state(out);out["removed"]=true;return device_operation_ack();
}
DeviceOperationResult project_fetch(DeviceApiContext &,JsonVariantConst request,JsonObject out){
  const char *url=request["url"].as<const char *>();const char *hash=request["sha256"].as<const char *>();
  if(!url||!hash||!request["bytes"].is<size_t>())return device_operation_error("character_project_resource_invalid",400);
  std::string error;if(!character::request_fetch(url,hash,request["bytes"].as<size_t>(),error))return device_operation_error(error.c_str(),409);
  character::fill_state(out);out["accepted"]=true;return device_operation_ack(202,::lunaredu::lunaros::OperationCompletion::Accepted);
}
constexpr const char *empty_schema=R"({"type":"object","additionalProperties":false})";
constexpr const char *revision_schema=R"({"type":"object","additionalProperties":false,"required":["revision"],"properties":{"revision":{"type":"string","pattern":"^[a-f0-9]{64}$"}}})";
constexpr const char *fetch_schema=R"({"type":"object","additionalProperties":false,"required":["url","sha256","bytes"],"properties":{"url":{"type":"string","maxLength":1536,"pattern":"^https://"},"sha256":{"type":"string","pattern":"^[a-f0-9]{64}$"},"bytes":{"type":"integer","minimum":1,"maximum":24576}}})";
constexpr DeviceOperationDefinition operations[]={
  device_operation_remote(device_operation_schemas(make_device_json_query("character.project.state.get",project_state,kNetworkServiceExecutor),empty_schema,nullptr),DeviceRemoteAccess::Query),
  make_device_bulk_write("character.project.install",project_install,device_operation_on_executor(kNetworkServiceExecutor,device_operation_body_limit(kDeviceDocumentBodyBytes,device_operation_method(DeviceOperationMethod::Post)))),
  device_operation_remote(device_operation_schemas(make_device_json_command("character.project.activate",project_activate,device_operation_on_executor(kNetworkServiceExecutor)),revision_schema,nullptr),DeviceRemoteAccess::Command),
  device_operation_remote(device_operation_schemas(make_device_json_command("character.project.remove",project_remove,device_operation_on_executor(kNetworkServiceExecutor)),empty_schema,nullptr),DeviceRemoteAccess::Command),
  device_operation_remote(device_operation_schemas(make_device_json_command("character.project.fetch",project_fetch,device_operation_on_executor(kNetworkServiceExecutor)),fetch_schema,nullptr),DeviceRemoteAccess::Command),
};
}
const lunaros::DeviceOperationDefinition *character_project_operations(size_t &count){count=std::size(operations);return operations;}
}
