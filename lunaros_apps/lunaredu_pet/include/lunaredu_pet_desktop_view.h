#pragma once
#include <stdbool.h>
#include "lvgl.h"
#ifdef __cplusplus
namespace lunaredu::pet {bool register_desktop_view();}
extern "C" {
#endif
void lunaros_pet_prepare_route(const char *next_route);
void lunaros_pet_refresh_subtitle(void);
void lunaros_pet_interrupt(void);
void lunaros_pet_view_init(void);
#ifdef __cplusplus
}
#endif
