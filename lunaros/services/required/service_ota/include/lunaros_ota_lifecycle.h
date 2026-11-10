#pragma once

#include "lunaredu_ota.h"

namespace lunaredu::lunaros {

// Framework-owned OTA lifecycle and metadata; products use lunaredu_ota.h.
struct OtaPartitionSnapshot {
  std::string label;
  uint32_t address = 0;
  uint32_t size = 0;
};

struct OtaImageMetadataSnapshot {
  std::string project_name;
  std::string version;
  std::string build_date;
  std::string build_time;
  std::string app_elf_sha256;
  OtaPartitionSnapshot running;
  OtaPartitionSnapshot boot;
  OtaPartitionSnapshot next;
};

OtaImageMetadataSnapshot ota_writer_metadata();
void setup_ota_service();
// Upkeep is advanced by the framework product lifecycle on main.
void tick_ota_service();
void ota_service_set_available_update(const OtaAvailableUpdate &update);
void ota_service_report_preparing(const char *message);

// Installer-owned readiness and transaction reservation (not authorization).
bool ota_service_prepare_maintenance(std::string &error);
void ota_service_release_maintenance();
bool ota_service_reserve_pull(std::string &error);
void ota_service_release_pull();
bool ota_service_install_pull_stream(const OtaFirmwareStream &, uint32_t job_id);

// HTTPS source progress is published through the public status snapshot.
void ota_service_report_pull_queued(uint32_t job_id);
void ota_service_report_pull_started(uint32_t job_id);
void ota_service_report_pull_finished(uint32_t job_id);

const DeviceCapabilityDescriptor &ota_capability_descriptor();
}
