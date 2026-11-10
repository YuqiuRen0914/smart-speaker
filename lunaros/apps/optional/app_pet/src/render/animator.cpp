#include "animator.hpp"
#include "palette.hpp"
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <cmath>
namespace lunaredu::pet::render {
using std::sin;using std::cos;
static float clamp(float x,float a,float b){return std::clamp(x,a,b);}
static float maxf(float a,float b){return std::max(a,b);}
static float K2(float n){return n<.5f?4*n*n*n:1-std::pow(-2*n+2,3)/2;}
void Spring::step(float f,float d,float dt){v+=(-2*d*f*v-f*f*(x-t))*dt;x+=v*dt;}
float Animator::rand(float a,float b){random^=random<<13;random^=random>>17;random^=random<<5;return a+(b-a)*(random>>8)*(1.0f/16777216);}
void Animator::reset(){*this=Animator{};weights[0]=from[0]=1;select(sleeping);}
void Animator::morph_to(int index,float stiffness){
    if(index==eye_to&&morph.t==1)return;
    from=weights;eye_to=index;morph={0,0,1};eye_stiffness=stiffness;
}
void Animator::select(int value){
    state=std::clamp(value,0,int(StateCount)-1);state_at=time;eye_override=-1;
    eye_idx=0;const auto c=character::expression_config(size_t(state),configs[state]);morph_to(c.eyes[0]);
    eye_until=time+rand(c.hold[0],c.hold[1])*.001f;
    blink_until=time+rand(c.blink[0],c.blink[1])*.001f;gaze_until=time+rand(.5f,1.4f);wink_until=time+rand(3,8);
    ctx={};ctx.nodUntil=time*1000+1800;ctx.impulseAt=time*1000+rand(500,1200);
    ctx.stAt=time*1000+(state==excited?rand(400,1100):state==searching?rand(800,1600):state==working?rand(1200,2400):rand(6000,10000));
    if(state!=waking&&state!=sleeping&&state!=drowsy)blink_at=time;
    ov_at=time;ov_rest=false;
}
void Animator::next_gaze(){
    float gx=rand(-.4f,.4f),gy=rand(-.3f,.3f),a=2.5f,b=5;
    switch(state){
    case idle:gx=gy=0;a=2.5f;b=5.5f;break;
    case listening:case dictating:gx=rand(-.3f,.3f);gy=rand(-.25f,.25f);a=2.2f;b=4.2f;break;
    case thinking:gx=sign()*rand(.5f,1);gy=-rand(.4f,1);a=1.5f;b=2.8f;break;
    case searching:gx=sign()*rand(.7f,1);gy=rand(-1,1);a=.55f;b=1.15f;break;
    case working:gx=rand(-.4f,.4f);gy=rand(.4f,1);a=1.2f;b=2.4f;break;
    case excited:gx=rand(-1,1);gy=rand(-1,.3f);a=.7f;b=1.4f;break;
    case surprised:gx=gy=0;a=1.6f;b=2.6f;break;
    case suspicious:gx=sign();gy=.3f;a=2.2f;b=4.2f;break;
    case angry:gx=rand(-.2f,.2f);gy=.2f;a=1.8f;b=3.2f;break;
    case drowsy:gx=rand(-.4f,.4f);gy=rand(.4f,1);a=2.5f;b=4.5f;break;
    case happy:gx=rand(-.7f,.7f);gy=-rand(0,.6f);a=1.8f;b=3.4f;break;
    case curious:gx=sign()*rand(.6f,1);gy=rand(-1,1);a=.95f;b=1.9f;break;
    case confused:gx=sign()*rand(.5f,1);gy=rand(-.6f,1);a=1.1f;b=2.3f;break;
    case bored:gx=sign()*rand(.7f,1);gy=rand(.4f,.9f);a=3;b=6;break;
    case proud:gx=rand(-.3f,.3f);gy=-rand(.3f,.7f);a=2.6f;b=4.6f;break;
    case shy:gx=sign()*rand(.6f,1);gy=rand(.5f,1);a=2;b=4;break;
    case sad:gx=rand(-.3f,.3f);gy=rand(.6f,1);a=2.8f;b=5;break;
    case laughing:gx=rand(-.5f,.5f);gy=-rand(.2f,.6f);a=.8f;b=1.7f;break;
    case scared:gx=sign()*rand(.7f,1);gy=rand(-.6f,.6f);a=.45f;b=1.05f;break;
    case playful:gx=sign()*rand(.5f,1);gy=-rand(0,.6f);a=.9f;b=1.8f;break;
    case notifying:{bool look=rand(0,1)<.72f;gx=look?.45f:.1f;gy=look?-.3f:-.05f;a=1.2f;b=2.4f;break;}
    default:break;
    }
    gaze_x.t=gx*15;gaze_y.t=gy*9;gaze_until=time+rand(a,b);
}
static bool has_overlay(int s){switch(s){case thinking:case orbit:case radar:case progress:case spawning:case dictating:case sending:case receiving:case uploading:case bouncing:case loading:case powering_down:case writing:case alerting:return true;default:return false;}}
static float effect_radius(int s){switch(s){case thinking:return 22;case dictating:return 16;case sending:case receiving:case uploading:return 20;case bouncing:return 18;case loading:return 15;case writing:return 17;case alerting:case powering_down:return 13;default:return 19;}}
static std::atomic<bool> flexible_body{true};
static std::atomic<int> mailbox{-1};
static std::atomic<int> color_mailbox{-1};
// Shape and explicit eye selection are independent presentation channels.
// Keeping them in the state mailbox caused a same-frame state command to
// overwrite a preceding shape command before the animator consumed either.
static std::atomic<int> shape_mailbox{-1};
static std::atomic<int> eye_mailbox{-1};
static std::atomic<int> touch_active_mailbox{0};
static std::atomic<int> touch_x_mailbox{0};
static std::atomic<int> touch_y_mailbox{0};
static std::atomic<int> progress_mailbox{-1};
void request(int c){mailbox.store(c,std::memory_order_relaxed);}
void request_color(int c){color_mailbox.store(c,std::memory_order_relaxed);}
void request_shape(int c){shape_mailbox.store(c,std::memory_order_relaxed);}
void request_eye(int c){eye_mailbox.store(c,std::memory_order_relaxed);}
void request_touch(bool active,float x,float y){
    touch_x_mailbox.store(int(clamp(x,-1.5f,1.5f)*1000),std::memory_order_relaxed);
    touch_y_mailbox.store(int(clamp(y,-1.5f,1.5f)*1000),std::memory_order_relaxed);
    touch_active_mailbox.store(active?1:0,std::memory_order_release);
}
void request_progress(float fraction){
    progress_mailbox.store(fraction < 0 ? -1 :
                           int(clamp(fraction,0,1)*10000),
                           std::memory_order_relaxed);
}
void Animator::update(float dt,PetRenderSnapshot &out){
    const uint32_t revision=character::active_generation();
    if(revision!=project_generation){project_generation=revision;select(state);}
    const bool flexible=flexible_body.load(std::memory_order_relaxed);
    time+=dt;int command=mailbox.exchange(-1,std::memory_order_relaxed);
    int pigment_command=color_mailbox.exchange(-1,std::memory_order_relaxed);
    int shape_command=shape_mailbox.exchange(-1,std::memory_order_relaxed);
    int eye_command=eye_mailbox.exchange(-1,std::memory_order_relaxed);
    const bool touch_active=touch_active_mailbox.load(std::memory_order_acquire)!=0;
    touch_x.t=touch_active?touch_x_mailbox.load(std::memory_order_relaxed)/1000.0f:0;
    touch_y.t=touch_active?touch_y_mailbox.load(std::memory_order_relaxed)/1000.0f:0;
    touch_pressure.t=touch_active?1:0;
    if(pigment_command==0x2000000)color_custom=0;
    else if(pigment_command>=0x1000000&&pigment_command<=0x1ffffff){body_color=uint32_t(pigment_command)&0xffffffu;color_custom=1;}
    if(command>=0&&command<StateCount){mode=1;select(command);}
    else if(command==100){mode=-1;show_index=0;show_at=time;select(sleeping);}
    else if(command==101){mode=0;select(idle);}
    else if(command>=300&&command<300+ShapeCount){shape_from=shape_to;shape_to=command-300;shape_at=time;}
    else if(command>=400&&command<425){mode=1;select(idle);eye_override=command-400;morph_to(eye_override);}
    if(shape_command>=0&&shape_command<ShapeCount){shape_from=shape_to;shape_to=shape_command;shape_at=time;}
    if(eye_command>=0&&eye_command<25){mode=1;select(idle);eye_override=eye_command;morph_to(eye_override);}
    // IMU input changes only the 2D expression; no spatial simulation runs.
    float sensor_tilt=std::hypot(out.tilt_x,out.tilt_y);
    float rotation=std::abs(out.turn_rate);
    if(rotation>4.5f||out.shake>13){
        reaction=std::max(reaction,std::min(1.0f,out.shake/20.0f));react_until=time+1.2f;
        int response=rotation>4.5f?confused:scared;
        if(mode!=1&&state!=response)select(response);
    }
    reaction*=std::exp(-dt*4);
    bool reacting=time<react_until;
    // The showcase contains only named states, eye contours, and shapes.
    // Real sensor events take priority.
    constexpr int shows=StateCount+25+ShapeCount;
    if(mode==-1&&!reacting){
        float dwell=show_index<StateCount?6:show_index<StateCount+25?2.4f:2.8f;
        if(time-show_at>=dwell){show_at=time;show_index=(show_index+1)%shows;
            if(show_index<StateCount)select(show_index);
            else if(show_index<StateCount+25){select(idle);eye_override=show_index-StateCount;morph_to(eye_override);}
            else{select(happy);shape_from=shape_to;shape_to=show_index-StateCount-25;shape_at=time;}
            if(show_index==0){shape_from=shape_to;shape_to=0;shape_at=time;}
        }else if((state==scared||state==confused||state==surprised)&&show_index!=state&&time-state_at>1.3f)select(show_index<StateCount?show_index:playful);
    }
    if(mode==0&&!reacting){
        int wanted=sensor_tilt>.12f?playful:rotation>.5f?curious:state==sleeping?sleeping:state==drowsy?(time-state_at>12?sleeping:drowsy):time-state_at>35?drowsy:idle;
        if(state!=wanted)select(wanted);
    }
    float ms=time*1000,t=time-state_at;Pose p=pose(time,t,ms);
    if(state==receiving){
        const int cycle=static_cast<int>(std::floor(t/1.7f));
        if(cycle!=receive_tick){receive_tick=cycle;receive_direction=rand(-pi*1.25f,pi*.25f);}
    }
    spin.t=p.spin;x.t=p.tx;y.t=p.ty;squash.t=p.squash;eye_scale.t=p.eyeBoost;
    y.v+=ctx.tyKick;ctx.tyKick=0;spin.v+=ctx.spinKick;ctx.spinKick=0;
    if(ctx.forceSleepEye){ctx.forceSleepEye=false;morph_to(13,11);}
    if(ctx.wakeEye){morph_to(int(ctx.wakeEye.a),ctx.wakeEye.b);ctx.wakeEye={};}
    if(ctx.wantBurst){bursts++;ctx.wantBurst={};ctx.wakingBurst=true;}
    if(ctx.wantBlink||(ctx.wakeBlink&&!ctx.wakingBlinked)){blink_at=time;ctx.wakingBlinked=true;ctx.wantBlink=false;}ctx.wakeBlink=false;
    const auto c=character::expression_config(size_t(state),configs[state]);
    if(eye_override<0&&time>=eye_until&&state!=sleeping&&state!=waking){eye_idx=(eye_idx+1+int(rand(0,c.count-1)))%c.count;morph_to(c.eyes[eye_idx],state==searching||state==excited?10:6);eye_until=time+rand(c.hold[0],c.hold[1])*.001f;}
    if(c.blink[1]>0&&time>=blink_until){blink_at=time;double_blink=rand(0,1)<.14f;blink_until=time+rand(c.blink[0],c.blink[1])*.001f;}
    float blink_t=time-blink_at;
    blink.t=blink_t<.15f?.05f:blink_t<.30f?1.08f:p.lid;
    if(double_blink&&blink_t>=.37f&&blink_t<.48f)blink.t=.05f;
    if(time>=gaze_until)next_gaze();
    bool winkable=state==idle||state==happy||state==excited||state==curious||state==playful;
    if(winkable&&time>=wink_until){wink_at=time;wink_eye=rand(0,1)<.5f?0:1;wink_until=time+rand(4.5f,10);}
    bool on=has_overlay(state);
    const int external_progress=progress_mailbox.load(std::memory_order_relaxed);
    if(on&&effect_state!=state){
        if(effect_state>=0&&overlay.x>.02f){
            previous_effect_state=effect_state;
            previous_effect_state_at=effect_state_at;
            previous_effect_ov_at=effect_ov_at;
            previous_receive_direction=receive_direction;
            previous_progress_fraction=last_progress_fraction;
            overlay_mix={0,0,1};
        }else{
            previous_effect_state=-1;
            overlay_mix={1,0,1};
        }
        effect_state=state;
        effect_state_at=state_at;
        effect_ov_at=ov_at;
    }
    if((state==progress&&external_progress<0)||state==spawning){float duration=state==progress?2.5f:2;if(!ov_rest&&time-ov_at>duration){ov_rest=true;ov_rest_at=time;}else if(ov_rest&&time-ov_rest_at>1.5f){ov_rest=false;ov_at=time;effect_ov_at=time;}on=!ov_rest;}
    if(reacting&&mode!=1)on=false;
    if(on!=ov_on){overlay_turn.t+=pi*sign();ov_on=on;}overlay.t=on?1:0;
    float spin_rate=state==loading?3:1.6f;
    if(state==humming||state==loading){
        float r=t<.5f?7*K2(t/.5f):t<1.3f?7+(spin_rate-7)*K2((t-.5f)/.8f):spin_rate+.3f*sin(t*.5f);
        spin_angle+=r*dt;
        // Both ribbon and orbit rendering multiply this angle by 0.85. Keep
        // the rendered phase bounded so long-running animations do not force
        // costly large-argument trigonometry or lose float precision.
        constexpr float kRenderedSpinScale=0.85f;
        constexpr float kSpinPeriod=2*pi/kRenderedSpinScale;
        if(spin_angle>=kSpinPeriod) spin_angle=std::fmod(spin_angle,kSpinPeriod);
    }
    int steps=int(std::ceil(dt*120));float h=dt/steps;
    for(int i=0;i<steps;i++){
        morph.step(eye_stiffness,1,h);
        if(flexible){spin.step(5,.9f,h);x.step(3.5f,1,h);y.step(4,1,h);squash.step(10,.8f,h);}
        else{spin.x=spin.v=x.x=x.v=y.x=y.v=0;squash.x=1;squash.v=0;}
        blink.step(26,1,h);eye_scale.step(9,.85f,h);gaze_x.step(13,1,h);gaze_y.step(13,1,h);overlay.step(14,1,h);overlay_mix.step(11,1,h);overlay_turn.step(14,1,h);notify.step(9,.55f,h);
        touch_x.step(18,1,h);touch_y.step(18,1,h);touch_pressure.step(14,.9f,h);
    }
    if(previous_effect_state>=0&&overlay_mix.x>.996f)previous_effect_state=-1;
    if(!has_overlay(state)&&overlay.x<.004f){effect_state=-1;previous_effect_state=-1;}
    float k=clamp(morph.x,0,1);for(int i=0;i<25;i++)weights[i]=from[i]*(1-k)+(i==eye_to?k:0);
    PetFrame &g=out.frame;g={};g.active=1;g.state=state;g.show_index=mode==-1?show_index:-1;
    g.body_color=body_color;g.color_custom=color_custom;
    g.time=time;g.state_time=t;g.overlay=clamp(overlay.x,0,1);
    g.effect_state=effect_state;g.effect_state_time=time-effect_state_at;
    g.overlay_time=time-effect_ov_at;g.fx_receive_direction=receive_direction;
    g.previous_effect_state=previous_effect_state;
    g.previous_effect_state_time=time-previous_effect_state_at;
    g.previous_overlay_time=time-previous_effect_ov_at;
    g.previous_receive_direction=previous_receive_direction;
    g.previous_progress_fraction=previous_progress_fraction;
    g.overlay_mix=clamp(overlay_mix.x,0,1);
    g.progress_fraction=external_progress<0?-1.0f:external_progress/10000.0f;
    if(state!=progress&&effect_state==progress)g.progress_fraction=last_progress_fraction;
    if(state==progress)last_progress_fraction=g.progress_fraction;
    notify.t=state==notifying?1:0;g.notify=notify.x;
    for(int i=0;i<25;i++)g.weights[i]=weights[i];
    g.eye_morph=k;
    float lids[2]={clamp(blink.x,.035f,1.3f),clamp(blink.x,.035f,1.3f)};
    float wt=(time-wink_at)/.32f;
    if(wt>=0&&wt<1){
        const float fraction=wt<.42f?1-wt/.42f:(wt-.42f)/.58f;
        lids[wink_eye]=maxf(lids[wink_eye]*clamp(fraction,0,1),.04f);
    }
    for(int i=0;i<2;i++)g.lids[i]=lids[i]*(1-reaction*.25f);
    g.eye_scale=eye_scale.x;g.gaze[0]=gaze_x.x/114.2705f;g.gaze[1]=gaze_y.x/114.2705f;
    float body=1-g.overlay,acting=reacting?.12f:1;
    // The upstream SVG runtime keeps the overlay body round; overlayTurn and
    // loading spin drive eye and particle motion in the display plane.
    g.turn=0;
    constexpr float shape_tilt[] = {
        1,1,1,1,1,.55f,.38f,1,.45f,1,1,1,.22f,1,.15f,.15f,1,1,1};
    const float tilt=shape_tilt[shape_from]+(shape_tilt[shape_to]-shape_tilt[shape_from])*K2(clamp((time-shape_at)/.8f,0,1));
    g.roll=spin.x*pi/180*body*acting*tilt;g.pitch=0;
    g.offset[0]=x.x/114.2705f*body*acting;g.offset[1]=-y.x/114.2705f*body*acting;
    const float overlay_radius=effect_radius(effect_state>=0?effect_state:state)/114.2705f*g.overlay;
    g.body_scale=body+overlay_radius;
    // The source applies sx=body+overlayRadius and sy=squash*body+
    // overlayRadius independently. Preserve that ratio through the raster
    // transform, including intermediate overlay spring frames.
    g.squash=flexible?(squash.x*body*acting+overlay_radius)/std::max(g.body_scale,.001f):1.0f;
    g.roundness=K2(clamp(g.overlay/.62f,0,1));g.opacity=1;
    const float tactile=clamp(touch_pressure.x,0,1);
    if(tactile>.001f){
        g.gaze[0]=clamp(touch_x.x,-1.0f,1.0f)*.13f;
        g.gaze[1]=clamp(touch_y.x,-1.0f,1.0f)*.10f;
        if(flexible){
        g.squash*=1-.055f*tactile;
        g.body_scale*=1-.018f*tactile;
        g.offset[0]+=clamp(touch_x.x,-1.0f,1.0f)*.025f*tactile;
        g.offset[1]-=clamp(touch_y.x,-1.0f,1.0f)*.018f*tactile;
        g.roll+=clamp(touch_x.x,-1.0f,1.0f)*.035f*tactile;
        }
    }
    if(state==thinking){float d=std::abs((std::fmod((time-ov_at)/1.4f+.119f,1.0f))-1.0f/3);d=std::min(d,1-d);float pulse=std::exp(-d*d/(2*.15f*.15f));g.offset[1]+=pulse*9/114.2705f*g.overlay;g.body_scale*=1+((.84f+.22f*pulse)-1)*g.overlay;g.opacity=1-.5f*(1-pulse)*g.overlay;}
    if(state==receiving){float u=clamp((std::fmod(t/1.7f,1.0f)-.58f)/.34f,0,1);g.body_scale*=1+.11f*sin(u*pi)*g.overlay;}
    if(state==sending){float u=std::fmod(t/1.5f,1.0f),kick=u<.18f?-.06f*sin(u/.18f*pi):u<.42f?.05f*sin((u-.18f)/.24f*pi):0;g.body_scale*=1+kick*g.overlay;}
    if(state==loading){g.offset[0]+=(sin(time*.9f)*2+sin(time*1.7f)*.8f)/114.2705f*g.overlay;g.offset[1]-=(sin(time*1.3f)*2.4f+sin(time*.6f)*1.2f)/114.2705f*g.overlay;}
    if(state==writing){
        float phase=std::fmod(t/2.5f,1.0f),px,py,angle,wig;
        if(phase<.68f){float u=phase/.68f,e=u*u*(3-2*u);px=-54+118*e;py=26;wig=sin(u*24)*3.2f*clamp(u/.08f,0,1)*clamp((1-u)/.08f,0,1);angle=17+sin(t*.6f);}
        else{float u=K2((phase-.68f)/.32f);px=64-118*u;py=26-20*sin(u*pi);wig=0;angle=17-2*sin(u*pi)+sin(t*.6f);}
        g.offset[0]+=px/114.2705f*g.overlay;g.offset[1]-=(py+wig*.5f)/114.2705f*g.overlay;g.roll+=angle*pi/180*g.overlay;
    }
    if(state==bouncing){float a=8*52/(.62f*.62f),drop=std::sqrt(80/a),height;if(t<drop)height=40-.5f*a*t*t;else{float u=std::fmod((t-drop)/.62f,1.0f);height=4*52*u*(1-u);}g.offset[1]-=(40-height)/114.2705f*g.overlay;}
    if(state==alerting){g.offset[1]-=58/114.2705f*g.overlay;g.body_scale*=1+.04f*std::exp(-std::fmod(t,2.2f)*5.5f)*g.overlay;}
    if(state==powering_down)g.opacity=1-(.28f+.2f*sin(time*1.6f))*g.overlay;
    float want_ribbon=(state==humming||state==loading)?1:0;ribbon+=(want_ribbon-ribbon)*(1-std::exp(-dt*(want_ribbon?5:2.6f)));g.ribbon=ribbon;g.fx_spin=spin_angle;g.burst=bursts;
    g.shape_from=shape_from;g.shape_to=shape_to;g.shape_mix=K2(clamp((time-shape_at)/.8f,0,1));
}
static Animator animator;
void reset(){mailbox.store(-1,std::memory_order_relaxed);color_mailbox.store(-1,std::memory_order_relaxed);shape_mailbox.store(-1,std::memory_order_relaxed);eye_mailbox.store(-1,std::memory_order_relaxed);touch_active_mailbox.store(0,std::memory_order_relaxed);touch_x_mailbox.store(0,std::memory_order_relaxed);touch_y_mailbox.store(0,std::memory_order_relaxed);progress_mailbox.store(-1,std::memory_order_relaxed);animator.reset();}
void update(float dt,PetRenderSnapshot &out){const auto revision=character::active_generation();animator.update(dt,out);character::mark_rendered(revision);}
}
extern "C" const char *pet_render_state_name(int state){return state>=0&&state<lunaredu::pet::render::StateCount?lunaredu::pet::render::names[state]:"unknown";}
extern "C" void pet_render_touch(int active,float x,float y){lunaredu::pet::render::request_touch(active!=0,x,y);}
extern "C" void pet_render_set_progress_fraction(float fraction){lunaredu::pet::render::request_progress(fraction);}
extern "C" int pet_render_set_state(const char *state){
    if(!state)return 0;
    for(int i=0;i<lunaredu::pet::render::StateCount;i++)
        if(!std::strcmp(state,lunaredu::pet::render::names[i])){
            lunaredu::pet::render::request(i);return 1;
        }
    return 0;
}
extern "C" int pet_render_set_color(const char *value){
    if(!value)return 0;
    for(int i=0;i<lunaredu::pet::render::BodyColorCount;i++)if(!std::strcmp(value,lunaredu::pet::render::body_colors[i].name)){
            // Every named entry is an explicit solid pigment. In particular,
            // "gold" is a real user-selectable color, not an alias for
            // clearing a previously selected product color.
            lunaredu::pet::render::request_color(int(0x1000000u|lunaredu::pet::render::body_colors[i].rgb));return 1;
    }
    if(value[0]=='#'&&std::strlen(value)==7){
        uint32_t rgb=0;
        for(int i=1;i<=6;i++){
            char c=value[i];int digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
            if(digit<0)return 0;
            rgb=(rgb<<4)|uint32_t(digit);
        }
        lunaredu::pet::render::request_color(int(0x1000000u|rgb));return 1;
    }
    return 0;
}
extern "C" int pet_render_set_shape(const char *shape){
    if(!shape)return 0;
    for(int i=0;i<lunaredu::pet::render::ShapeCount;i++)
        if(!std::strcmp(shape,lunaredu::pet::render::shape_names[i])){
            lunaredu::pet::render::request_shape(i);return 1;
        }
    return 0;
}
extern "C" int pet_render_command(const char *s){
    if(!s)return 0;
    if(pet_render_set_state(s))return 1;
    if(!std::strcmp(s,"auto")){lunaredu::pet::render::request(100);return 1;}
    if(!std::strcmp(s,"sensor")){lunaredu::pet::render::request(101);return 1;}
    if(!std::strncmp(s,"color ",6))return pet_render_set_color(s+6);
    if(!std::strncmp(s,"shape ",6))return pet_render_set_shape(s+6);
    if(!std::strncmp(s,"eye ",4)){char *end=nullptr;long n=std::strtol(s+4,&end,10);if(end!=s+4&&*end==0&&n>=0&&n<25){lunaredu::pet::render::request_eye(int(n));return 1;}}
    return 0;
}

extern "C" void pet_render_set_flexible(int enabled){lunaredu::pet::render::flexible_body.store(enabled!=0,std::memory_order_relaxed);}
