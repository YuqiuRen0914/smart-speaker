#include "device_api/adapters/application_projection.h"
#include "device_api/device_operation_builders.h"
#include "model/device_model.h"
#include "system/system_projection.h"
#include "device_api/device_api_context.h"

#include "bsp/lunaros_board_facts.h"
#include "device_api/bootstrap_metrics_json.h"
#include "app/codec/application_maintenance_json.h"

#include <ArduinoJson.h>
#include <initializer_list>
#include "system/system_info.h"



#include "device_api/device_projection.h"

namespace lunaredu::lunaros {

namespace {

void fill_board_facts([[maybe_unused]] DeviceApiContext &device, JsonObject payload) {
  const auto board = lunaredu::lunaros::board_support_facts();
  payload["board_id"] = board.id;
  payload["board_type"] = board.type;
  payload["model"] = board.model;
  payload["board_hardware_revision"] = board.hardware_revision;
  payload["board_support_status"] = board.support_status;
}

void fill_system_info([[maybe_unused]] DeviceApiContext &device, JsonObject payload) {
  fill_application_maintenance_payload(device_api_application_maintenance(device), payload["application_maintenance"].to<JsonObject>());
  fill_board_facts(device, payload);
  const auto info = lunaredu::system::read_system_info_snapshot();
  payload["chip"] = info.chip_model_name;
  payload["cores"] = info.chip_cores;
  payload["revision"] = info.chip_revision;
  payload["mac"] = info.mac_address;
  payload["flash_bytes"] = info.flash_size;
  payload["internal_free_bytes"] = info.internal_free_bytes;
  payload["minimum_free_heap_bytes"] = info.minimum_free_heap_size;
  payload["psram_free_bytes"] = info.psram_free_bytes;
  payload["psram_total_bytes"] = info.psram_total_bytes;
  payload["uptime_ms"] = info.uptime_ms;
  fill_bootstrap_metrics_payload(device, payload["bootstrap"].to<JsonObject>());
  payload["firmware"] = info.application_version;
  payload["application"] = info.application_name;
  payload["idf_version"] = info.idf_version;
  payload["running_partition"] = info.running_ota_partition;
  auto partitions = payload["partitions"].to<JsonArray>();
  for (const auto &partition : info.partitions) {
    auto item = partitions.add<JsonObject>();
    item["label"] = partition.label;
    item["bytes"] = partition.size;
    item["type"] = partition.type;
  }
}

const DeviceCapabilityDescriptor kSystemCapability{"system_info"};
void fill_application_cleanup(DeviceApiContext &device, JsonObject payload) { fill_application_maintenance_payload(device_api_application_maintenance(device), payload); }
DeviceOperationResult retry_application_cleanup(DeviceApiContext &device, JsonVariantConst, JsonObject payload) {
  const auto &maintenance = device_api_application_maintenance(device);
  if (!maintenance.retry || !maintenance.retry(maintenance.context)) return {false, 503, "applications_service_unavailable"};
  fill_application_maintenance_payload(device_api_application_maintenance(device), payload);
  return device_operation_ack(202,::lunaredu::lunaros::OperationCompletion::Accepted);
}
constexpr DeviceOperationDefinition kSystemOperations[] = {
  make_device_json_provider("info.get", fill_system_info),
  device_operation_on_caller(make_device_json_provider("applications.cleanup.status", fill_application_cleanup)),
  device_operation_on_caller(make_device_json_command("applications.cleanup.retry", retry_application_cleanup)),

};
const DeviceModuleProjectionMetadata kSystemModule{"system", "system"};
const DeviceModuleProjection kSystemProjection{
  "system_info", &kSystemModule, kSystemOperations, std::size(kSystemOperations)};
} // namespace

void fill_system_base_capabilities(DeviceApiContext &, JsonObject payload) {
  payload["contract_version"] = 2;
  payload["operation_request_correlation"] = true;
  // Retain the wire fields for clients when no optional provider is selected.
  // Actual capability observation belongs to the optional Diagnostics owner.
  for(const char *field:{"diagnostics_available","diagnostic_capture_switch",
      "diagnostic_capture_default_enabled","diagnostic_inventory",
      "diagnostic_operation_trace","diagnostic_activity_summaries",
      "diagnostic_checks","cpu_monitor_supported"})payload[field]=false;
  payload["diagnostics_session_required"]=true;
  payload["diagnostics_on_demand"]=true;
}
DeviceOperationsView system_base_operations() {
  return {kSystemOperations, std::size(kSystemOperations)};
}
const DeviceProjectionContribution &system_base_projection(){
  static constexpr DeviceMetadataProjection metadata={"system.board","board",fill_board_facts,fill_board_facts};
  static const DeviceProjectionContribution contribution{&kSystemCapability,&kSystemProjection,&metadata,true};
  return contribution;
}
}
