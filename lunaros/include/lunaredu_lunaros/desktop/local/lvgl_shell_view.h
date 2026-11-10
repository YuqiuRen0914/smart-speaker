#pragma once

#include <cstdint>
#include "lunaredu_lunaros/desktop/local/local_display.h"

namespace lunaredu::desktop {

// Shared display bootstrap. Products supply their shell and boot-stage plan.
bool setup_lvgl_shell_view(const LocalDisplayConfig &config);
bool lvgl_shell_view_ready();
void report_lvgl_shell_boot_stage(uint8_t stage, bool ready);
void request_lvgl_shell_boot_finish();
void tick_lvgl_shell_view();
bool lvgl_shell_boot_animation_complete();

} // namespace lunaredu::desktop
