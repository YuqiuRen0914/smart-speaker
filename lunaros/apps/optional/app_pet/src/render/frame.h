#pragma once
#include <stdint.h>
// Appearance state only. Never used as a force, collider, or physics position.
typedef struct {
    float weights[25],lids[2],gaze[2],eye_scale,eye_morph;
    float turn,roll,pitch,offset[2],squash,body_scale,roundness,opacity;
    float time,state_time,overlay,overlay_time,ribbon,fx_spin,fx_receive_direction,shape_mix,notify;
    float effect_state_time,overlay_mix,previous_effect_state_time,previous_overlay_time;
    float previous_receive_direction,previous_progress_fraction;
    float progress_fraction; // -1 uses the animation's own cycle; 0..1 is product time.
    int state,effect_state,previous_effect_state,show_index,shape_from,shape_to;
    uint32_t burst;
    int active;
    uint32_t body_color;
    int color_custom;
} PetFrame;
