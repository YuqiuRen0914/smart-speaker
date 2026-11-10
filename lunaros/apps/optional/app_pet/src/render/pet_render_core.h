#pragma once
#include <stdint.h>
#include "frame.h"
#include "lunaredu_pet_frame_region.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    // IMU inputs projected onto the character's flat display plane.
    float tilt_x,tilt_y,turn_rate,shake;
    int valid;
    PetFrame frame;
} PetRenderSnapshot;
typedef struct {
    uint32_t clear_us,body_us,eyes_us,total_us;
    uint32_t back_ribbon_us,effects_us,body_shape_us,eye_shape_us,front_ribbon_us;
    uint32_t total_task_us,body_task_us,eyes_task_us,effects_task_us;
    int task_profiled;
} PetRenderProfile;
void pet_render_set_flexible(int enabled);
void pet_render_reset(float ax,float ay,float az);
PetRenderSnapshot pet_render_sample(float ax,float ay,float az,float gx,float gy,float gz,float dt);
void pet_render_frame(PetFrameBuffer frame,const PetRenderSnapshot *snapshot,int status);
// Foreground owner has reconstructed the black backdrop, including retained
// surfaces which erase only previous character and overlay coverage.
void pet_render_frame_cleared(PetFrameBuffer frame,const PetRenderSnapshot *snapshot,int status);
void pet_render_invalidate(void);
PetRegion pet_render_bounds(void);
PetRenderProfile pet_render_profile(void);
int pet_render_project_touch(int x,int y,float *local_x,float *local_y);
void pet_render_touch(int active,float local_x,float local_y);
int pet_render_command(const char *command);
int pet_render_set_state(const char *state);
int pet_render_set_shape(const char *shape);
int pet_render_set_color(const char *color);
void pet_render_set_progress_fraction(float fraction);
const char *pet_render_state_name(int state);
#ifdef __cplusplus
}
#endif
