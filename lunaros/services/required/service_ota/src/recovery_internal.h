#pragma once
#include "lunaredu_ota_recovery.h"
#include "recovery_contract.h"
#include <esp_partition.h>
namespace lunaredu::lunaros {
bool ota_recovery_validate_header(const uint8_t *data, size_t size, std::string &error);
const esp_partition_t *ota_recovery_write_target(std::string &error);
bool ota_recovery_begin_write(size_t size, std::string &error);
bool ota_recovery_finish_write(const esp_partition_t *target, std::string &error);
void ota_recovery_write_failed();
bool ota_recovery_repair_state(std::string &error);
bool ota_recovery_select_factory_for_rescue(std::string &error);
const esp_partition_t *ota_recovery_boot_write_target(size_t size, std::string &error);
bool ota_recovery_finish_boot_write(const esp_partition_t *target, std::string &error);
}
