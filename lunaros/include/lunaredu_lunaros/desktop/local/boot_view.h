#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
// Descriptors are copied; strings must remain valid for the display lifetime.
// Configure on the LVGL owner before starting a boot view. Maximum eight stages.
typedef struct {
    const char *glyph;
    const char *label;
    const char *pending_detail;
    const char *degraded_detail;
} LunarOSBootStage;
bool lunaros_configure_boot_stages(const LunarOSBootStage *stages, uint8_t count);
void lunaros_set_boot_status(
    uint8_t ready_mask,
    uint8_t degraded_mask,
    int finish_requested
);
int lunaros_ui_boot_animation_complete(void);

// Single LVGL-owner component. Owns boot objects/timer only; completion is
// delegated to the shell so startup navigation remains product-state driven.
void lunaros_boot_view_start(lv_obj_t *screen, void (*finished)(void));
bool lunaros_boot_view_active(void);

#ifdef __cplusplus
}
#endif
