#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    bool network_provisioning;
    bool network_connecting;
    bool network_connected;
    bool assistant_enabled;
    bool assistant_service_enabled;
    bool assistant_suspended;
    bool assistant_activation_ready;
    bool assistant_activation_pending;
    bool assistant_transport_connected;
    bool assistant_speaking;
    bool assistant_microphone_enabled;
    bool connection_notice_due;
    bool subtitles_enabled;
    bool full_screen_pet;
    bool desktop_page;
    bool reply_hold;
    bool transcript_hold;
    const char * session_state;
    const char * transcript_role;
    const char * transcript_text;
} lunaros_home_assistant_state_t;

const char * lunaros_home_assistant_badge_text(
    const lunaros_home_assistant_state_t *state);

const char * lunaros_home_assistant_subtitle_text(
    const lunaros_home_assistant_state_t *state);

bool lunaros_interaction_should_interrupt_assistant_on_press(
    bool assistant_speaking);


#ifdef __cplusplus
}
#endif
