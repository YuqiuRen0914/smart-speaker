#ifndef LUNAROS_UI_PORT_H
#define LUNAROS_UI_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Host boundary for the portable LunarOS LVGL UI core. The UI implementation
 * must only emit semantic events through this interface; browser and firmware
 * integrations provide their own port implementation.
 */
int lunaros_ui_port_read_touch_raw(uint16_t * raw_x, uint16_t * raw_y);
void lunaros_ui_port_emit_intent(const char *type, const char *payload);
int lunaros_ui_port_submit_wifi_credentials(const char *ssid,
    const char *password);
void lunaros_ui_port_screen_changed(const char *page);
void lunaros_ui_port_ready(void);

#ifdef __cplusplus
}
#endif

#endif
