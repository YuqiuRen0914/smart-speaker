#include "control/output_control.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include "lunaros_audio_bridge.h"
#include "pcm_backend.h"
#include "sample_ring.h"
using namespace lunaredu::audio;
static bool fail_factory=true, fail_write=false;
static std::vector<int16_t> written;
class FakeBackend : public PcmBackend {
public:
 int input_sample_rate() const override{return 16000;}
 int output_sample_rate() const override{return 16000;}
 bool enable_input(bool) override{return true;}
 bool enable_output(bool) override{return true;}
 bool configure_output_sample_rate(uint32_t) override{return true;}
 bool read_input(int16_t *p,size_t n,uint32_t) override{std::fill(p,p+n,1);return true;}
 bool write_output(const int16_t *p,size_t n,uint16_t gain_q15,uint32_t) override{
  written.resize(n);
  for(size_t i=0;i<n;++i) written[i]=static_cast<int16_t>((static_cast<int64_t>(p[i])*gain_q15+(p[i]>=0?16384:-16384))/32767);
  return !fail_write;
 }
};
namespace lunaredu::audio {
std::unique_ptr<PcmBackend> create_pcm_backend(
    lunaredu::lunaros::KernelRuntime *, const char *, std::string &error) {
 if(fail_factory){error="injected";return nullptr;} return std::make_unique<FakeBackend>();
}
}
int main() {
 lunaredu::lunaros::OutputControl control;
 std::string control_error;
 assert(control.register_provider({"audio", "audio", 0, false}, control_error));
 assert(control.register_consumer({"foreground", "audio", 50}, control_error));
 assert(control.seal(control_error));
 assert(!local_pcm_audio_bridge_input_enabled());
 assert(!start_local_pcm_audio_bridge(control,"audio")); fail_factory=false;
 assert(start_local_pcm_audio_bridge(control,"audio"));
 assert(!local_pcm_audio_bridge_input_enabled());
 assert(local_pcm_audio_bridge_enable_input(true));assert(local_pcm_audio_bridge_input_enabled());
 assert(local_pcm_audio_bridge_enable_input(false));assert(!local_pcm_audio_bridge_input_enabled());
 int16_t disabled_input[2]{};assert(!local_pcm_audio_bridge_read_input(disabled_input,2,0));
 assert(local_pcm_audio_bridge_enable_output(true));
 assert(local_pcm_audio_bridge_set_output_volume(50));
 const int16_t input[]={100,200,-100,-200,300}; int16_t output[5];
 assert(local_pcm_audio_bridge_write_output(input,5,0));
 assert(written[0]==25 && written[4]==75);
 assert(local_pcm_audio_bridge_read_output_reference(output,5));
 assert(output[0]==50 && output[1]==-25 && output[2]==-50 && output[3]==75 && output[4]==0);
 assert(local_pcm_audio_bridge_status().reference_drops==1);
 fail_write=true; assert(!local_pcm_audio_bridge_write_output(input,5,0));
 assert(local_pcm_audio_bridge_read_output_reference(output,5) && output[0]==0);
 fail_write=false; assert(local_pcm_audio_bridge_write_output(input,5,0));
 assert(local_pcm_audio_bridge_enable_output(false));
 assert(local_pcm_audio_bridge_read_output_reference(output,5) && output[0]==0);
 puts("PASS audio backend retry, perceptual volume, reference, failed write and disable cleanup");
}
