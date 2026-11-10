#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include "lunaros_ota_lifecycle.h"

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

OtaWriterImageStatus ota_writer_image_status();
bool ota_writer_select_rescue(std::string &error);
bool ota_writer_confirm_image(std::string &error);
bool ota_writer_rollback_and_reboot(std::string &error);
using OtaWriterPrecommitCallback = void (*)(const std::string &elf_sha256,
                                           size_t image_size, uint32_t target_address);
bool ota_writer_begin(size_t image_size, std::string &error,
                      bool maintenance_authorized = false,
                      OtaWriterPrecommitCallback precommit = nullptr,
                      bool bootstrap_update = false);
bool ota_writer_write(const uint8_t *data, size_t size,
                           std::string &error);
bool ota_writer_complete(std::string &error);
std::string ota_writer_last_candidate_elf_sha256();
bool ota_writer_last_candidate_selected();
void ota_writer_abort();
void ota_writer_cooperative_yield();

} // namespace lunaredu::lunaros
