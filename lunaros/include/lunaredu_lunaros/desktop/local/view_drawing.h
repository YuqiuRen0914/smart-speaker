#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
void lunaros_ui_reset_object(lv_obj_t *obj);
void lunaros_ui_draw_rect(lv_layer_t *layer, int x, int y, int width, int height,
    lv_color_t color, int radius, lv_opa_t opacity);
void lunaros_ui_draw_bordered_rect(lv_layer_t *layer, int x, int y, int width,
    int height, lv_color_t background, lv_color_t border, int radius);
void lunaros_ui_draw_text(lv_layer_t *layer, const char *text, const lv_font_t *font,
    lv_color_t color, lv_text_align_t alignment, int x, int y, int width, int height);
void lunaros_ui_draw_lunar_logo_mark(lv_layer_t *layer, int x, int y);

#ifdef __cplusplus
}
#endif
