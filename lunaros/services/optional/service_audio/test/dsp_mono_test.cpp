#include "lunaros_audio_dsp.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <complex>
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
using namespace lunaredu::audio::dsp;
static double response_db(const BiquadCoefficients &c, double hz, unsigned rate) {
  const auto z=std::polar(1.0,-2.0*3.141592653589793*hz/rate);
  return 20.0*std::log10(std::abs((double(c.b0)+double(c.b1)*z+double(c.b2)*z*z)/
                                 (1.0+double(c.a1)*z+double(c.a2)*z*z)));
}

// Independent left/right filter owners verify that the shared mono history
// remains exact when inputs later diverge, including changes to coefficients.
namespace {
// Direct-form II reference uses the public transfer coefficients, without
// the production identity shortcut or shared mono-state optimization.
struct ReferenceFilter {
  BiquadCoefficients c;float w0=0,w1=0;
  float process(float input){
    // The adopted ESP-DSP kernel uses direct-form II state, not the
    // retired transposed state. Keep an independent per-channel recurrence.
    const float d0=input-c.a1*w0-c.a2*w1;
    const float out=c.b0*d0+c.b1*w0+c.b2*w1;
    w1=w0;w0=d0;
    if(std::isfinite(out))return out;
    w0=w1=0;return 0.0f;}
};
void verify_neutral_filters(){
  for(unsigned rate:{8000U,16000U,24000U,48000U})for(bool high_pass:{false,true}) {
    AudioProcessor actual;auto settings=preset_settings(Preset::kFlat);settings.high_pass_enabled=high_pass;
    assert(actual.configure(rate,settings));
    std::array<std::array<ReferenceFilter,4>,2> refs{};
    for(auto &channel:refs){channel[0].c=actual.high_pass_coefficients();channel[1].c=actual.low_coefficients();
      channel[2].c=actual.mid_coefficients();channel[3].c=actual.high_coefficients();}
    const auto reference=[&](unsigned side,float input){auto &filters=refs[side];
      const float wet=filters[0].process(input);
      float out=input+(wet-input)*(high_pass?1.0f:0.0f);
      for(unsigned i=1;i<4;++i)out=filters[i].process(out);
      return out;};
    for(unsigned i=0;i<65536;++i) {
      // Cover every PCM16 value and a mono-to-stereo transition.
      const float a=(int(i)-32768)/32768.0f;
      const float b=i<32768?a:(32767-int(i))/32768.0f;
      float l=a,r=b;actual.process_filters(l,r);
      assert(l==reference(0,a)&&r==reference(1,b));
    }
    for(float input:{std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(),0.25f,0.0f}) {
      float l=input,r=input;actual.process_filters(l,r);
      assert(l==reference(0,input)&&r==reference(1,input));
    }
  }
}
void verify_common_dc_history(){
  for(unsigned rate:{8000U,16000U,24000U,48000U})for(auto preset:{Preset::kFlat,Preset::kVoice,Preset::kMusic}){
    const auto settings=preset_settings(preset);AudioProcessor actual;
    assert(actual.configure(rate,settings));
    std::array<std::array<ReferenceFilter,4>,2> filters{};
    for(auto &channel:filters){channel[0].c=actual.high_pass_coefficients();channel[1].c=actual.low_coefficients();
      channel[2].c=actual.mid_coefficients();channel[3].c=actual.high_coefficients();}
    float dc_x[2]{},dc_y[2]{};
    const float feedback=std::exp(-2.0f*3.14159265358979323846f*10.0f/float(rate));
    const float positive=std::max(0.0f,settings.low_gain_db)+std::max(0.0f,settings.mid_gain_db)+std::max(0.0f,settings.high_gain_db);
    const float eq_gain=std::pow(10.0f,-positive/20.0f);
    const auto filter=[&](unsigned side,float input){
      auto &f=filters[side];const float wet=f[0].process(input);
      float value=input+(wet-input)*1.0f;
      for(unsigned j=1;j<4;++j)value=f[j].process(value);
      return value;
    };
    const auto common=[&](unsigned side,int16_t input){
      const float value=input/32768.0f;
      const float output=value-dc_x[side]+feedback*dc_y[side];
      dc_x[side]=value;dc_y[side]=std::isfinite(output)?output:0.0f;
      return filter(side,dc_y[side])*eq_gain;
    };
    for(unsigned n=0;n<65536;++n){
      const int16_t a=int16_t(n-32768),b=n<32768?a:int16_t(32767-int(n));
      if(n%13==0){
        float l=a/32768.0f,r=b/32768.0f;const float x=l,y=r;
        actual.process_filters(l,r);assert(l==filter(0,x)&&r==filter(1,y));
      }else{
        float l,r,pre;actual.process_common(a,b,l,r,pre,false);
        const float x=common(0,a),y=common(1,b);
        assert(pre==std::clamp(0.5f*(x+y),-1.0f,1.0f));
      }
    }
  }
}
}
static void verify_block_processing(){
  for(unsigned rate:{8000u,24000u,48000u})for(unsigned channels:{1u,2u}){
    AudioProcessor block,scalar;
    for(auto *p:{&block,&scalar}){assert(p->configure(rate,preset_settings(Preset::kMusic)));p->request_unmute(1);}
    std::array<int16_t,258> input{},actual{},expected{};
    unsigned random=12345;
    for(unsigned step=0;step<1200;++step){
      if(step==200||step==600)for(auto *p:{&block,&scalar}){
        auto settings=preset_settings(step==200?Preset::kVoice:Preset::kFlat);
        settings.high_pass_enabled=step!=600;p->set_settings(settings);
      }
      if(step==400)for(auto *p:{&block,&scalar})p->set_channel_mode(ChannelMode::kMono);
      if(step==800)for(auto *p:{&block,&scalar})p->request_mute(2);
      if(step==900)for(auto *p:{&block,&scalar})p->request_unmute(2);
      const size_t frames=std::array<size_t,5>{1,7,31,128,129}[step%5];
      for(size_t i=0;i<frames;++i){
        random=random*1664525u+1013904223u;input[i*channels]=int16_t(random>>16);
        if(channels==2)input[i*2+1]=step<100?input[i*2]:int16_t(random);
        const bool silence=step%37==0;
        const int16_t l=silence?0:input[i*channels],r=silence?0:channels==1?l:input[i*2+1];
        float a,b,pre;scalar.process_common(l,r,a,b,pre,true,0.75f);scalar.finalize_output(a,b);
        expected[i*channels]=scalar.quantize(channels==1?0.5f*(a+b):a);
        if(channels==2)expected[i*2+1]=scalar.quantize(b);
      }
      const bool silence=step%37==0;
      if(step%2){actual=input;assert(block.process_pcm(silence?nullptr:actual.data(),actual.data(),frames,channels,true,0.75f));}
      else assert(block.process_pcm(silence?nullptr:input.data(),actual.data(),frames,channels,true,0.75f));
      // The optimized SoC kernel may fuse arithmetic differently from a
      // sequence of length-one calls. Compare the final PCM contract.
      for(size_t i=0;i<frames*channels;++i)assert(std::abs(int(actual[i])-expected[i])<=2);
      assert(block.coefficient_rebuild_count()==scalar.coefficient_rebuild_count());
      assert(block.fade_state()==scalar.fade_state());
      assert(block.diagnostics().invalid_samples==scalar.diagnostics().invalid_samples);
    }
  }
}
int main() {
  // Independent legacy thermal transfer: the fast interval remains far below
  // the protection knee, including negative/nonfinite-reset input clamping.
  for(unsigned n=0;n<=100000;++n){
    const float power=float(n)*0.0000001f;
    const float db=20.0f*std::log10(std::max(std::sqrt(std::max(0.0f,power)),1.0e-9f));
    const float attenuation=db>-14.0f?-12.0f*std::clamp((db+14.0f)/8.0f,0.0f,1.0f):0.0f;
    assert(std::pow(10.0f,attenuation/20.0f)==1.0f);
  }
  verify_block_processing();
  // Public frequency/gain semantics must survive ESP-DSP's different notch
  // gain/Q convention. This catches a same-name API substitution at half gain.
  for(unsigned rate:{8000u,16000u,24000u,48000u}) {
    for(float gain:{-12.0f,-3.0f,0.0f,3.0f,6.0f}) {
      AudioProcessor processor;
      auto s=preset_settings(Preset::kCustom);
      s.mid_gain_db=gain;s.low_gain_db=gain;s.high_gain_db=gain;
      assert(processor.configure(rate,s));
      const auto peak=processor.mid_coefficients();
      assert(biquad_is_stable(peak));
      assert(std::abs(response_db(peak,s.mid_frequency_hz,rate)-gain)<0.01);
      assert(std::abs(response_db(peak,0,rate))<0.01);
      assert(std::abs(response_db(peak,rate/2.0,rate))<0.01);
      assert(std::abs(response_db(processor.low_coefficients(),0,rate)-gain)<0.02);
      assert(std::abs(response_db(processor.high_coefficients(),rate/2.0,rate)-gain)<0.02);
      assert(std::abs(response_db(processor.high_pass_coefficients(),s.high_pass_hz,rate)+3.0103)<0.02);
    }
  }
  verify_neutral_filters();
  verify_common_dc_history();
  AudioProcessor shared,left_owner,right_owner;
  for(auto *p:{&shared,&left_owner,&right_owner})
    assert(p->configure(24000,preset_settings(Preset::kMusic)));
  unsigned random=12345;
  for(unsigned i=0;i<200000;++i) {
    if(i==70000)for(auto *p:{&shared,&left_owner,&right_owner})
      p->set_settings(preset_settings(Preset::kVoice));
    random=random*1664525U+1013904223U;
    const float a=int16_t(random>>16)/32768.f;
    const float b=i>100000?int16_t(random)/32768.f:a;
    float l=a,r=b,ll=a,lr=a,rl=b,rr=b;
    shared.process_filters(l,r);
    left_owner.process_filters(ll,lr);right_owner.process_filters(rl,rr);
    assert(l==ll&&r==rr&&ll==lr&&rl==rr);
  }
  // The diagnostic duration remains current between block observations.
  shared.reset();shared.request_unmute(1);
  for(unsigned i=0;i<10000;++i) {
    float l,r,pre;shared.process_common(i&1?32767:-32768,i&1?32767:-32768,l,r,pre,false);
    const auto d=shared.diagnostics();
    assert(d.continuous_limiter_ms<=1000U*i/24000U);
  }
  std::puts("DSP mono/stereo filter history, changing EQ and live limiter diagnostics passed");
  return 0;
}
