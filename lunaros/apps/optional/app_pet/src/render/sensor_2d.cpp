#include "pet_render_core.h"
#include "animator.hpp"
#include <algorithm>
#include <cmath>

namespace {
float origin_x=0,origin_y=0;
float previous_x=0,previous_y=0,previous_z=1;
}

extern "C" void pet_render_reset(float ax,float ay,float az){
    const bool valid=std::isfinite(ax)&&std::isfinite(ay)&&std::isfinite(az);
    origin_x=valid?ax:0;
    origin_y=valid?ay:0;
    previous_x=origin_x;
    previous_y=origin_y;
    previous_z=valid?az:1;
    lunaredu::pet::render::reset();
    lunaredu::pet::render::request(101); // Start with sensor-driven 2D animation.
}

extern "C" PetRenderSnapshot pet_render_sample(float ax,float ay,float az,float gx,float gy,float gz,float dt){
    PetRenderSnapshot out{};
    if(!std::isfinite(ax)||!std::isfinite(ay)||!std::isfinite(az)||
       !std::isfinite(gx)||!std::isfinite(gy)||!std::isfinite(gz)||
       !std::isfinite(dt)||dt<=0||dt>.1f)return out;
    // 30 Hz is 33.333 ms; ordinary late frames must not discard animation.
    // Animator keeps <=1/120 s integration steps (at most 12 for this bound).
    // Acceleration is in g; yaw is in degrees/second. The depth axis is used
    // only to detect a shake, never as a simulated position or force.
    out.tilt_x=ax-origin_x;
    out.tilt_y=ay-origin_y;
    out.turn_rate=gz*.0174532925f;
    const float dx=ax-previous_x,dy=ay-previous_y,dz=az-previous_z;
    out.shake=std::min(20.0f,std::sqrt(dx*dx+dy*dy+dz*dz)*9.80665f);
    previous_x=ax;previous_y=ay;previous_z=az;
    out.valid=1;
    lunaredu::pet::render::update(dt,out);
    return out;
}
