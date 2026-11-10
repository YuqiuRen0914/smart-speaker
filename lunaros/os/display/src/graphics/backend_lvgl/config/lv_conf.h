#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_USE_OS LV_OS_NONE
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (128 * 1024)
#define LV_MEM_POOL_INCLUDE "graphics_pool.h"
#define LV_MEM_POOL_ALLOC lunaros_graphics_allocate_pool
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_DRAW_UNIT_CNT 1
#define LV_USE_DRAW_SW_ASM LV_DRAW_SW_ASM_NONE
#define LV_DRAW_BUF_STRIDE_ALIGN 1
// Scene owns native RGB565/RGB565+A8/A8 source textures. Keep ARGB alpha
// layers for transformed rounded Canvas images and their intermediates.
// Panel byte swapping belongs to the display adapter, outside LVGL.
#define LV_DRAW_SW_SUPPORT_RGB565 1
#define LV_DRAW_SW_SUPPORT_ARGB8888 1
#define LV_DRAW_SW_SUPPORT_A8 1
#define LV_DRAW_SW_SUPPORT_RGB565A8 1
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 1
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 0
#define LV_DRAW_SW_SUPPORT_RGB888 0
#define LV_DRAW_SW_SUPPORT_XRGB8888 0
#define LV_DRAW_SW_SUPPORT_L8 0
#define LV_DRAW_SW_SUPPORT_AL88 0
#define LV_DRAW_SW_SUPPORT_I1 0
#define LV_USE_THORVG 0
#define LV_USE_TINY_TTF 1
#define LV_TINY_TTF_FILE_SUPPORT 0
#define LV_TINY_TTF_CACHE_GLYPH_CNT 0
#define LV_TINY_TTF_CACHE_KERNING_CNT 0
#define LV_USE_FREETYPE 0
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0
#define LV_USE_BUTTON 1
#define LV_USE_LABEL 1
#define LV_USE_BAR 1
#define LV_USE_SWITCH 1
#define LV_USE_FLEX 1
#define LV_USE_GRID 1
#define LV_USE_LIST 0
// Enable additional widgets deliberately when a consumer and budget exist.
#define LV_USE_ANIMIMG 0
#define LV_USE_ARC 1
#define LV_USE_ARCLABEL 0
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 1
#define LV_USE_CHART 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMAGE 1
// Scene owns source snapshots and revision reuse. In-memory descriptors need
// no additional decoded-image/header cache; transforms belong to LVGL.
#define LV_CACHE_DEF_SIZE 0
#define LV_IMAGE_HEADER_CACHE_DEF_CNT 0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_LINE 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SLIDER 1
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 1
#define LV_USE_TEXTAREA 1
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_LOG 0
#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0
#define LV_FONT_MONTSERRAT_14 1
#endif
