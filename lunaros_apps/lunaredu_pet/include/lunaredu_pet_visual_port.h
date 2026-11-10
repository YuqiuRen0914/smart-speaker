#pragma once
#include <stdint.h>
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
// Optional product binding, installed on the UI owner before mounting Pet.
// Pet owns the containing widgets; the renderer releases its timers on unmount.
typedef struct {
    void (*mount)(lv_obj_t *parent, uint16_t scale_percent);
    void (*unmount)(void);
    void (*sync)(void);
    void (*set_audio_priority)(int enabled);
} pet_visual_ports_t;
void pet_view_configure_visual(pet_visual_ports_t ports);
void pet_view_sync_visual(void);
#ifdef __cplusplus
}
#endif
