#pragma once
#include "lunaredu_pet_desktop_view_api.h"
typedef struct {
    char device_serial_number[40];
    bool assistant_enabled;
    bool assistant_service_enabled;
    bool assistant_suspended;
    bool assistant_speaking;
    bool assistant_microphone_enabled;
    bool assistant_activation_ready;
    bool assistant_activation_pending;
    char assistant_activation_code[24];
    bool assistant_transport_connected;
    char assistant_session_state[20];
    char assistant_last_error[64];
    char assistant_transcript_text[160];
    char assistant_transcript_role[12];
    uint32_t assistant_transcript_revision;
    bool network_provisioning;
    bool network_connected;
    bool network_connecting;
} pet_view_model_t;
#ifdef __cplusplus
extern "C" {
#endif
pet_view_model_t *pet_view_model(void);
void pet_view_mount(lv_obj_t *, const char *, bool);
void pet_view_leave(const char *);

void pet_view_begin_update(void);
bool pet_view_end_update(void);
bool pet_view_take_change(void);
#ifdef __cplusplus
}
#endif
