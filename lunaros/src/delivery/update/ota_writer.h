#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lunaredu::lunaros {

struct OtaWriterImageStatus {
  bool rollback_enabled = false;
  bool pending_verify = false;
  bool signature_verification_enabled = false;
  bool anti_rollback_enabled = false;
  bool flash_encryption_enabled = false;
  std::string state;
  std::string error;
};

struct OtaWriterPartitionInfo {
  std::string label;
  uint32_t address = 0;
  uint32_t size = 0;
};

struct OtaWriterMetadata {
  std::string project_name;
  std::string version;
  std::string build_date;
  std::string build_time;
  OtaWriterPartitionInfo running;
  OtaWriterPartitionInfo boot;
  OtaWriterPartitionInfo next;
};

OtaWriterImageStatus ota_writer_image_status();
OtaWriterMetadata ota_writer_metadata();
bool ota_writer_confirm_image(std::string &error);
bool ota_writer_rollback_and_reboot(std::string &error);
bool ota_writer_begin(size_t image_size, std::string &error);
bool ota_writer_write(const uint8_t *data, size_t size,
                           std::string &error);
bool ota_writer_complete(std::string &error);
void ota_writer_abort();
void ota_writer_cooperative_yield();

} // namespace lunaredu::lunaros
