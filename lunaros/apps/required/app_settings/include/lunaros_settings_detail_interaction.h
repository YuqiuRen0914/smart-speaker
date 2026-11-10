#pragma once
#include "lunaros_settings_detail.h"
#include "lunaros_settings_menu.h"
#include "desktop/ui/provisioning_step.h"
#include <cstdint>

namespace lunaredu::settings {
struct DetailInteraction { bool leave = false, repaint = false, start_provisioning = false; };
DetailInteraction update_detail_interaction(Detail detail, desktop::ProvisioningStep &step,
    const SystemPresentation &model, bool confirm, bool back, int navigation,
    int64_t now_us, int64_t &last_refresh_us);
} // namespace lunaredu::settings
