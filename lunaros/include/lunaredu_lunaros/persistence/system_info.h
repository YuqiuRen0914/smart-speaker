#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lunaredu::system {

struct SystemPartitionSnapshot {
  std::string label;
  uint8_t type = 0;
  uint8_t subtype = 0;
  uint32_t address = 0;
  uint32_t size = 0;
};

struct SystemInfoSnapshot {
  uint64_t flash_size = 0;
  uint64_t uptime_ms = 0;
  uint64_t internal_free_bytes = 0;
  uint64_t psram_free_bytes = 0;
  uint64_t psram_total_bytes = 0;
  uint64_t minimum_free_heap_size = 0;
  std::string mac_address;
  std::string chip_model_name;
  int chip_model = 0;
  uint32_t chip_cores = 0;
  uint32_t chip_revision = 0;
  uint32_t chip_features = 0;
  std::string application_name;
  std::string application_version;
  std::string application_compile_time;
  std::string idf_version;
  std::string elf_sha256;
  std::vector<SystemPartitionSnapshot> partitions;
  std::string running_ota_partition;
};

SystemInfoSnapshot read_system_info_snapshot();

class SystemInfo {
public:
  static size_t GetFlashSize();
  static size_t GetMinimumFreeHeapSize();
  static std::string GetMacAddress();
  static std::string GetChipModelName();
};

} // namespace lunaredu::system
