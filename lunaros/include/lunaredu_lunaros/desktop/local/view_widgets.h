#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif

// Shared shell widgets. Called only by the active view on the LVGL owner.
lv_obj_t * lunaros_view_create_label(lv_obj_t * parent, const char * text, const lv_font_t * font, lv_color_t color);
void lunaros_view_style_surface(lv_obj_t * obj, lv_color_t background, int radius);
void lunaros_view_style_pressable(lv_obj_t * obj);
lv_obj_t * lunaros_view_create_primary_button(lv_obj_t * parent, const char * title, int x, int y, int width);
void lunaros_view_create_page_title(const char * title, const char * detail);
void lunaros_view_create_fixed_page_footer(size_t * page_index, size_t page_count);
void lunaros_view_create_file_icon(lv_obj_t * parent, int x, int y, int kind);
void lunaros_view_create_folder_icon(lv_obj_t * parent, int x, int y, int width, int height);
void lunaros_view_emit_intent(const char * type, const char * payload);
bool lunaros_view_emit_string_intent(const char * type, const char * key,
    const char * value);
void lunaros_view_navigate(const char *route);
void lunaros_view_refresh(void);
const char *lunaros_view_active_route(void);

bool lunaros_view_is_initialized(void);
void lunaros_view_create_status_bar(lv_obj_t * screen);
void lunaros_view_navigation_event(lv_event_t * event);
void lunaros_view_set_label_text(lv_obj_t * label, const char * text,
    const lv_font_t * font);
lv_obj_t * lunaros_view_create_icon_part(lv_obj_t * parent, int x, int y, int width, int height, lv_color_t color, int radius);
lv_obj_t * lunaros_view_create_wifi_status_icon(lv_obj_t * parent, int quality);
lv_obj_t * lunaros_view_create_lock_icon(lv_obj_t * parent);
bool lunaros_view_setup_active(void);
void lunaros_view_setup_advance(bool start);
void lunaros_view_refresh_ambient(void);
void lunaros_view_return_from_overlay(void);
#ifdef __cplusplus
}
#endif
