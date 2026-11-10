#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void lunaros_set_eyes_mode(int active);

void lunaros_set_assistant_state(
    int activation_ready,
    int activation_pending,
    const char * activation_code,
    int transport_connected,
    const char * session_state,
    const char * last_error,
    int service_enabled,
    int suspended,
    int speaking
);

void lunaros_set_assistant_transcript(
    const char * role,
    const char * text,
    uint32_t revision
);

void lunaros_set_pet_frame_a8rgb666_u8x4(const uint8_t * pixels, int width, int height);

int lunaros_ui_pet_display_is_companion(void);
void lunaros_set_pet_companion_mode(int enabled);


void lunaros_set_pet_preferences(
    int subtitles_enabled,
    int continuous_conversation);

#ifdef __cplusplus
}
#endif
