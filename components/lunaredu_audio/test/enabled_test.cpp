#include <cassert>
#include <cstdio>
#include <vector>
#include "lunaredu_audio_bridge.h"
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
 bool write_output(const int16_t *p,size_t n,uint32_t) override{written.assign(p,p+n);return !fail_write;}
};
namespace lunaredu::audio {
std::unique_ptr<PcmBackend> create_pcm_backend(std::string &error) {
 if(fail_factory){error="injected";return nullptr;} return std::make_unique<FakeBackend>();
}
}
int main(){
 assert(!setup_local_pcm_audio_bridge()); fail_factory=false;
 assert(setup_local_pcm_audio_bridge());
 assert(local_pcm_audio_bridge_enable_output(true));
 assert(local_pcm_audio_bridge_set_output_volume(50));
 const int16_t input[]={100,200,-100,-200,300}; int16_t output[5];
 assert(local_pcm_audio_bridge_write_output(input,5,0));
 assert(written[0]==50 && written[4]==150);
 assert(local_pcm_audio_bridge_read_output_reference(output,5));
 assert(output[0]==100 && output[1]==-50 && output[2]==-100 && output[3]==150 && output[4]==0);
 fail_write=true; assert(!local_pcm_audio_bridge_write_output(input,5,0));
 assert(local_pcm_audio_bridge_read_output_reference(output,5) && output[0]==0);
 fail_write=false; assert(local_pcm_audio_bridge_write_output(input,5,0));
 assert(local_pcm_audio_bridge_enable_output(false));
 assert(local_pcm_audio_bridge_read_output_reference(output,5) && output[0]==0);
 puts("PASS audio backend retry, volume, ring overflow, failed write and disable cleanup");
}
