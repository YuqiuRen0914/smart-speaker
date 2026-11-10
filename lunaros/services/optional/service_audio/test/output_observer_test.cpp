#include "control/output_control.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <vector>
#include <deque>
#include "sample_ring.h"
#include "lunaros_audio_bridge.h"
#include "pcm_backend.h"
#include "rate_cvt_test_control.h"
using namespace lunaredu::audio;

class DeferredBackend : public PcmBackend {
public:
 OutputObserver observer;
 PcmBackendDiagnostics stats;
 std::vector<int16_t> queued;
 bool paused=false;
 bool duplex=false,bad_duplex=false;
 unsigned input_reads=0;
 int rate=24000;
 int input_sample_rate() const override { return 16000; }
 int output_sample_rate() const override { return rate; }
 bool enable_input(bool) override { return true; }
 bool enable_output(bool enabled) override {
  if (!enabled) { stats.discarded_frames+=stats.pending_frames;stats.pending_frames=0;queued.clear(); }
  return true;
 }
 bool pause_output(bool value) override { paused=value;return true; }
 bool configure_output_sample_rate(uint32_t value) override { rate=int(value);return true; }
 bool read_input(int16_t *samples,size_t count,uint32_t) override {++input_reads;std::fill_n(samples,count,123);return true;}
 bool has_duplex_reference()const override{return duplex;}
 bool read_duplex(int16_t *mic,int16_t *ref,size_t count,uint32_t,PcmDuplexObservation &value)override {
  std::fill_n(mic,count,123);std::fill_n(ref,count,321);
  value.synchronous_reference=!bad_duplex;value.sample_rate=16000;value.reference_samples=count;
  value.sample_clock_valid=true;value.capture_generation=7;value.first_sample=640;
  value.reference_delay_known=true;value.reference_delay_samples=24;return true;
 }
 bool write_output(const int16_t *p,size_t n,uint16_t,uint32_t) override {
  queued.assign(p,p+n);stats.submitted_frames+=n;stats.pending_frames+=n;return true;
 }
 bool set_output_observer(OutputObserver cb) override { observer=std::move(cb);return true; }
 PcmBackendDiagnostics diagnostics() const override { return stats; }
 void complete(bool success=true) {
  if (paused) return;
  stats.pending_frames-=queued.size();
  if (success) {
   stats.completed_frames+=queued.size();
   for (auto &sample:queued) sample/=4; // Represents post-volume/DSP output.
   observer(queued.data(),queued.size(),rate);
  } else { ++stats.output_write_failures;stats.discarded_frames+=queued.size(); }
  queued.clear();
 }
};
static DeferredBackend *backend=nullptr;
namespace lunaredu::audio {
std::unique_ptr<PcmBackend> create_pcm_backend(lunaredu::lunaros::KernelRuntime *,const char *,std::string &) {
 auto result=std::make_unique<DeferredBackend>();backend=result.get();backend->stats.realtime=true;return result;
}
}
int main() {
 // Bulk reference operations must keep exact overwrite/drop/sample ordering.
 for(size_t capacity:{size_t(1),size_t(7),size_t(256)}){
  SampleRing ring;assert(ring.initialize(capacity));std::deque<int16_t> expected;
  uint32_t random=71;int16_t source[700],out[700];
  for(unsigned n=0;n<5000;++n){
   random=random*1664525u+1013904223u;const auto count=size_t(random%700);
   if(n%3==0){
    for(size_t i=0;i<count;++i)source[i]=int16_t(n+i);
    size_t lost=0;for(size_t i=0;i<count;++i){if(expected.size()==capacity){expected.pop_front();++lost;}expected.push_back(source[i]);}
    assert(ring.push(source,count)==lost);
   }else{
    const auto take=std::min(count,expected.size());
    if(n%3==1){assert(ring.pop(out,count)==take);for(size_t i=0;i<take;++i){assert(out[i]==expected.front());expected.pop_front();}}
    else {assert(ring.discard(count)==take);for(size_t i=0;i<take;++i)expected.pop_front();}
   }
   assert(ring.size()==expected.size());
  }
 }
 lunaredu::lunaros::OutputControl control;
 std::string control_error;
 assert(control.register_provider({"audio", "audio", 0, false}, control_error));
 assert(control.register_consumer({"foreground", "audio", 50}, control_error));
 assert(control.seal(control_error));
 assert(start_local_pcm_audio_bridge(control,"audio"));
 assert(local_pcm_audio_bridge_enable_output(true));
 int16_t input[]={40,80,120,160,200,240},reference[4]={};
 assert(local_pcm_audio_bridge_write_output(input,6,0));
 assert(local_pcm_audio_bridge_status().pending_output_samples==6);
 const auto progress=local_pcm_audio_bridge_progress();
 assert(progress.initialized&&progress.output_enabled&&progress.realtime_pipeline);
 assert(progress.pending_output_samples==6&&progress.submitted_output_samples==6);
 assert(local_pcm_audio_bridge_read_output_reference(reference,4) && reference[0]==0);
 assert(local_pcm_audio_bridge_pause_output(true));backend->complete();
 assert(local_pcm_audio_bridge_status().pending_output_samples==6);
 assert(local_pcm_audio_bridge_pause_output(false));
 const auto before_conversion=rate_cvt_test::calls,before_open=rate_cvt_test::opens;
 backend->complete();
 assert(rate_cvt_test::calls==before_conversion&&rate_cvt_test::opens==before_open);
 assert(local_pcm_audio_bridge_status().pending_output_samples==0);
 assert(local_pcm_audio_bridge_status().completed_output_samples==6);
 assert(local_pcm_audio_bridge_read_output_reference(reference,4));
 assert(rate_cvt_test::calls>before_conversion);
 // This host API double checks ownership/counts, not official FIR waveforms.
 assert(reference[0]==20 && reference[1]==30 && reference[2]==50 && reference[3]==60);
 assert(local_pcm_audio_bridge_write_output(input,6,0));backend->complete(false);
 assert(local_pcm_audio_bridge_status().hardware_write_failures==1);
 assert(local_pcm_audio_bridge_status().discarded_output_samples==6);
 assert(local_pcm_audio_bridge_progress().hardware_write_failures==1);
 assert(local_pcm_audio_bridge_progress().discarded_output_samples==6);
 assert(local_pcm_audio_bridge_read_output_reference(reference,4) && reference[0]==0);
 assert(local_pcm_audio_bridge_write_output(input,6,0));backend->complete();
 const auto stale_at=std::chrono::steady_clock::now()+std::chrono::milliseconds(130);
 while (std::chrono::steady_clock::now()<stale_at) {}
 assert(local_pcm_audio_bridge_read_output_reference(reference,4) && reference[0]==0);
 assert(local_pcm_audio_bridge_write_output(input,6,0));
 assert(local_pcm_audio_bridge_enable_output(false));
 assert(local_pcm_audio_bridge_enable_input(true));
 int16_t mic[4],ref[4];PcmCaptureObservation capture;
 const auto input_reads=backend->input_reads;
 assert(local_pcm_audio_bridge_read_capture(mic,ref,4,20,&capture));
 assert(backend->input_reads==input_reads+1&&capture.duplex.reference_source==PcmReferenceSource::SoftwareOutput);
 assert(!capture.duplex.synchronous_reference&&!capture.duplex.reference_delay_known);
 backend->duplex=true;const auto converted=rate_cvt_test::calls;
 assert(local_pcm_audio_bridge_read_capture(mic,ref,4,20,&capture));
 assert(ref[0]==321&&mic[0]==123&&capture.duplex.synchronous_reference&&capture.reference.first_sample==640);
 assert(capture.duplex.reference_delay_known&&capture.duplex.reference_delay_samples==24&&rate_cvt_test::calls==converted);
 backend->bad_duplex=true;
 assert(!local_pcm_audio_bridge_read_capture(mic,ref,4,20,&capture));
 assert(backend->input_reads==input_reads+1); // Advertised hardware reference cannot silently become software.
 backend->duplex=false;
 assert(local_pcm_audio_bridge_status().pending_output_samples==0);
 assert(local_pcm_audio_bridge_enable_output(true));
 assert(local_pcm_audio_bridge_write_output(input,6,0));backend->complete();
 rate_cvt_test::fail_process=true;
 PcmReferenceObservation observation;
 assert(!local_pcm_audio_bridge_read_output_reference(reference,4,&observation));
 assert(observation.conversion_failed&&observation.available_samples==0&&reference[0]==0);
 const auto failed_calls=rate_cvt_test::calls;
 assert(!local_pcm_audio_bridge_read_output_reference(reference,4,&observation));
 assert(rate_cvt_test::calls==failed_calls); // No repeated failing SDK work each tick.
 rate_cvt_test::fail_process=false;local_pcm_audio_bridge_clear_output_reference();
 assert(local_pcm_audio_bridge_write_output(input,6,0));backend->complete();
 assert(local_pcm_audio_bridge_read_output_reference(reference,4,&observation));
 assert(observation.source_discontinuous&&!observation.conversion_failed);
 assert(local_pcm_audio_bridge_configure_output(16000));
 assert(local_pcm_audio_bridge_write_output(input,6,0));backend->complete();
 const auto identity_calls=rate_cvt_test::calls;
 assert(local_pcm_audio_bridge_read_output_reference(reference,4,&observation));
 assert(observation.available_samples==4&&observation.first_sample==0);
 for(unsigned n=0;n<4;++n)assert(reference[n]==input[n]/4);
 assert(rate_cvt_test::calls==identity_calls);
 assert(local_pcm_audio_bridge_read_output_reference(reference,4,&observation));
 assert(observation.available_samples==2&&observation.first_sample==4);
 assert(reference[0]==input[4]/4&&reference[1]==input[5]/4&&reference[2]==0&&reference[3]==0);
 assert(local_pcm_audio_bridge_enable_output(false));
 assert(stop_local_pcm_audio_bridge(control_error));
 assert(rate_cvt_test::opens==rate_cvt_test::closes);
 puts("PASS deferred hardware completion, post-DSP reference, resampling, pause, failure and stale reference");
}
