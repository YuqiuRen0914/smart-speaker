#include "lunaredu_lunaros/persistence/system_info.h"

#include <stdio.h>

#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_chip_info.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>


namespace lunaredu::system {

SystemInfoSnapshot read_system_info_snapshot() {
  SystemInfoSnapshot snapshot;
  snapshot.uptime_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
  snapshot.internal_free_bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  snapshot.psram_free_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  snapshot.psram_total_bytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  snapshot.flash_size = static_cast<uint64_t>(SystemInfo::GetFlashSize());
  snapshot.minimum_free_heap_size =
      static_cast<uint64_t>(SystemInfo::GetMinimumFreeHeapSize());
  snapshot.mac_address = SystemInfo::GetMacAddress();
  snapshot.chip_model_name = SystemInfo::GetChipModelName();

  esp_chip_info_t chip_info = {};
  esp_chip_info(&chip_info);
  snapshot.chip_model = chip_info.model;
  snapshot.chip_cores = chip_info.cores;
  snapshot.chip_revision = chip_info.revision;
  snapshot.chip_features = chip_info.features;

  const esp_app_desc_t *app_desc = esp_app_get_description();
  if (app_desc != nullptr) {
    snapshot.application_name = app_desc->project_name;
    snapshot.application_version = app_desc->version;
    snapshot.application_compile_time =
        std::string(app_desc->date) + "T" + app_desc->time + "Z";
    snapshot.idf_version = app_desc->idf_ver;
    char sha256[65] = {};
    for (size_t index = 0; index < 32; ++index) {
      snprintf(sha256 + index * 2, sizeof(sha256) - index * 2, "%02x",
               app_desc->app_elf_sha256[index]);
    }
    snapshot.elf_sha256 = sha256;
  }

  esp_partition_iterator_t iterator = esp_partition_find(
      ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  while (iterator != nullptr) {
    const esp_partition_t *partition = esp_partition_get(iterator);
    if (partition != nullptr) {
      snapshot.partitions.push_back({
          .label = partition->label,
          .type = partition->type,
          .subtype = partition->subtype,
          .address = partition->address,
          .size = partition->size,
      });
    }
    iterator = esp_partition_next(iterator);
  }

  const esp_partition_t *ota_partition = esp_ota_get_running_partition();
  if (ota_partition != nullptr) {
    snapshot.running_ota_partition = ota_partition->label;
  }
  return snapshot;
}

} // namespace lunaredu::system
