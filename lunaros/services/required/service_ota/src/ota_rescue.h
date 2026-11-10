#pragma once
#include <string>
#include <esp_partition.h>

namespace lunaredu::lunaros {
// Startup-only legacy rescue: selects verified Bootstrap, never old business.
bool ota_select_bootstrap_rescue(std::string &error);
const esp_partition_t *ota_bootstrap_rescue_target(std::string &error);
// Explicit Boot repair keeps a confirmed running image, even if the inactive
// Boot is damaged. Unlike handoff, it does not require that target to boot.
bool ota_bootstrap_update_admitted(std::string &error);
bool ota_legacy_dual_layout_valid(std::string &error);
}
