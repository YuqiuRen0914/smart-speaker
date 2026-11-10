#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Mount the read-only product font partition and create the shared LunarOS
 * font sizes. Failure is non-fatal: every accessor keeps returning the
 * built-in rescue font so the settings and recovery UI remain usable.
 */
bool lunaros_font_runtime_setup(void);
bool lunaros_font_runtime_ready(void);
const char *lunaros_font_runtime_failure_reason(void);
size_t lunaros_font_runtime_asset_bytes(void);
size_t lunaros_font_runtime_missing_glyph_count(void);
size_t lunaros_font_runtime_sanitize_utf8(const lv_font_t *font,
    const char *input, char *output, size_t output_capacity,
    size_t *replacement_count);

const lv_font_t *lunaros_font_runtime_10(void);
const lv_font_t *lunaros_font_runtime_12(void);
const lv_font_t *lunaros_font_runtime_14(void);
const lv_font_t *lunaros_font_runtime_16(void);
const lv_font_t *lunaros_font_runtime_20(void);
const lv_font_t *lunaros_font_runtime_24(void);
const lv_font_t *lunaros_font_runtime_72(void);

#ifdef __cplusplus
}
#endif
