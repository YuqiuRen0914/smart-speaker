#pragma once
#include "pet_render_core.h"
#include "assets.hpp"
#include <array>
namespace lunaredu::pet::render {
constexpr float pi=3.14159265358979323846f;
struct Spring {float x=0,v=0,t=0;void step(float frequency,float damping,float dt);};
struct Pair {float a=0,b=0;explicit operator bool()const{return a!=0||b!=0;}};
struct Context {
    float nodUntil=1800,nodEnd=0,angryShakeUntil=0,impulseAt=600,tyKick=0,spinKick=0,slumpAt=0,stAt=6000;
    Pair wakeEye,wantBurst;
    bool forceSleepEye=false,wakeBlink=false,wakingBlinked=false,wantBlink=false,notifyPop=false,wakingBurst=false;
    int dragCycle=-1;
};
struct Extra {int eyeTo;float eyeMorphX,blinkX;};
struct Pose {float spin,tx,ty,squash,lid,eyeBoost;};
class Animator {
public:
    void reset();
    void update(float dt,PetRenderSnapshot &out);
    void select(int state);
    int mode=-1; // -1 showcase, 0 sensor-driven, 1 selected state.
private:
    uint32_t random=0x6a09e667,bursts=0;
    uint32_t project_generation=0;
    float time=0,state_at=0,eye_until=0,blink_until=0,gaze_until=0,wink_until=0,wink_at=-100;
    float blink_at=-100,show_at=0,react_until=0;
    float reaction=0,spin_angle=0,ribbon=0,ov_at=0,ov_rest_at=0;
    float effect_state_at=0,effect_ov_at=0,previous_effect_state_at=0,previous_effect_ov_at=0;
    float previous_receive_direction=0,previous_progress_fraction=-1,last_progress_fraction=-1;
    float receive_direction=-0.7f;
    int receive_tick=-1;
    bool ov_rest=false,ov_on=false;
    int state=idle,effect_state=-1,previous_effect_state=-1,eye_to=0,eye_idx=0,wink_eye=0,show_index=0;
    int shape_from=0,shape_to=0;
    uint32_t body_color=0;
    int color_custom=0;
    int eye_override=-1;
    bool double_blink=false;
    float shape_at=-100;
    std::array<float,25> from{},weights{};
    Context ctx;
    Spring morph{1,0,1},spin,x,y,squash{1,0,1},blink{1,0,1},eye_scale{1,0,1},gaze_x,gaze_y,overlay,overlay_mix{1,0,1},overlay_turn,notify;
    Spring touch_x,touch_y,touch_pressure;
    float rand(float a,float b);
    int sign(){return rand(0,1)<.5f?-1:1;}
    Pose pose(float mt,float dtState,float now);
    void morph_to(int index,float stiffness=8);
    void next_gaze();
    float eye_stiffness=8;
};
// Thread-safe mailbox: renderer/serial writes; the sensor task owns Animator.
void request(int command);
void request_touch(bool active,float x,float y);
void reset();
void update(float dt,PetRenderSnapshot &out);
}
