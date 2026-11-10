#include <cassert>
#include <iostream>
#include <string>

#include "lunaredu_lunaros/device_api/device_projection.h"
#include "device_api/projections/device_projection_internal.h"

using namespace lunaredu::lunaros;

namespace {

void fill_status(JsonObject) {}
void fill_info(JsonObject) {}
void fill_other_status(JsonObject) {}

} // namespace

int main() {
  setup_device_metadata_index();

  static constexpr DeviceMetadataProjection first = {
      "network.service", "network", fill_status, fill_info};
  std::string error;
  assert(register_device_metadata_index(first, &error));
  assert(register_device_metadata_index(first, &error));

  static constexpr DeviceMetadataProjection conflicting_id = {
      "network.service", "network", fill_other_status, fill_info};
  assert(!register_device_metadata_index(conflicting_id, &error));
  assert(error == "device_metadata_id_conflict");

  static constexpr DeviceMetadataProjection conflicting_section = {
      "network.alias", "network", fill_other_status, nullptr};
  assert(!register_device_metadata_index(conflicting_section, &error));
  assert(error == "device_metadata_status_section_conflict");

  assert(seal_device_metadata_index(&error));
  assert(device_metadata_index_sealed());

  static constexpr DeviceMetadataProjection late = {
      "storage.filesystem", "filesystem", fill_status, nullptr};
  assert(!register_device_metadata_index(late, &error));
  assert(error == "device_metadata_index_sealed");

  std::cout << "5 device metadata registry checks passed" << std::endl;
  return 0;
}
