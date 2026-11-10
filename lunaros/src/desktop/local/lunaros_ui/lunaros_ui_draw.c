#include "lunaredu_lunaros/desktop/local/view_drawing.h"
#include "lunaredu_lunaros/desktop/local/logo_assets.h"

void lunaros_ui_reset_object(lv_obj_t * obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

void lunaros_ui_draw_rect(lv_layer_t * layer, int x, int y, int width, int height,
    lv_color_t color, int radius, lv_opa_t opacity)
{
    lv_draw_rect_dsc_t descriptor;
    lv_draw_rect_dsc_init(&descriptor);
    descriptor.bg_color = color;
    descriptor.bg_opa = opacity;
    descriptor.radius = radius;
    lv_area_t area = {
        .x1 = x,
        .y1 = y,
        .x2 = x + width - 1,
        .y2 = y + height - 1,
    };
    lv_draw_rect(layer, &descriptor, &area);
}

void lunaros_ui_draw_bordered_rect(lv_layer_t * layer, int x, int y,
    int width, int height, lv_color_t background, lv_color_t border,
    int radius)
{
    lv_draw_rect_dsc_t descriptor;
    lv_draw_rect_dsc_init(&descriptor);
    descriptor.bg_color = background;
    descriptor.bg_opa = LV_OPA_COVER;
    descriptor.border_color = border;
    descriptor.border_opa = LV_OPA_COVER;
    descriptor.border_width = 1;
    descriptor.radius = radius;
    lv_area_t area = {
        .x1 = x,
        .y1 = y,
        .x2 = x + width - 1,
        .y2 = y + height - 1,
    };
    lv_draw_rect(layer, &descriptor, &area);
}

void lunaros_ui_draw_text(lv_layer_t * layer, const char * text,
    const lv_font_t * font, lv_color_t color, lv_text_align_t alignment,
    int x, int y, int width, int height)
{
    lv_draw_label_dsc_t descriptor;
    lv_draw_label_dsc_init(&descriptor);
    descriptor.text = text;
    descriptor.font = font;
    descriptor.color = color;
    descriptor.opa = LV_OPA_COVER;
    descriptor.align = alignment;
    lv_area_t area = {
        .x1 = x,
        .y1 = y,
        .x2 = x + width - 1,
        .y2 = y + height - 1,
    };
    lv_draw_label(layer, &descriptor, &area);
}

void lunaros_ui_draw_lunar_logo_mark(lv_layer_t * layer, int x, int y)
{
    lv_draw_image_dsc_t descriptor;
    lv_draw_image_dsc_init(&descriptor);
    descriptor.src = &lunaredu_logo_boot_image;
    const lv_area_t area = {
        .x1 = x,
        .y1 = y,
        .x2 = x + LUNAREDU_LOGO_BOOT_SIZE - 1,
        .y2 = y + LUNAREDU_LOGO_BOOT_SIZE - 1,
    };
    lv_draw_image(layer, &descriptor, &area);
}

