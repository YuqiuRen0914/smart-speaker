
#include "persistence/system/board_facts.h"

#include <ArduinoJson.h>
#include "lunaredu_lunaros/persistence/system_info.h"

#include "lunaredu_lunaros/device_api/device_projection.h"

namespace lunaredu::lunaros {

namespace {

void fill_board_facts(JsonObject payload) {
  const auto board = lunaredu::lunaros::board_support_facts();
  payload["board_id"] = board.id;
  payload["board_type"] = board.type;
  payload["model"] = board.model;
}

void fill_system_info(JsonObject payload) {
  fill_board_facts(payload);
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
constexpr DeviceOperationDefinition kSystemOperations[] = {
  make_device_json_provider("info.get", fill_system_info),
};
const DeviceModuleProjectionMetadata kSystemModule{"system", "system"};
const DeviceModuleProjection kSystemProjection{
  "system_info", &kSystemModule, kSystemOperations, std::size(kSystemOperations)};
} // namespace

bool setup_system_projection(std::string &error) {
  static constexpr DeviceMetadataProjection kBoardMetadata = {
      "system.board", "board", fill_board_facts, fill_board_facts};
  return register_device_projection(
      {&kSystemCapability, &kSystemProjection, &kBoardMetadata, true}, &error);
}

} // namespace lunaredu::lunaros
