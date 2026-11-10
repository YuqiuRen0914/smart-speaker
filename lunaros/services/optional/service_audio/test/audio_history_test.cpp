#include "lunaros_audio_history.h"
#include "lunaros_audio_dual_i2s.h"
#include "kernel/memory_status.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
using namespace lunaredu;
namespace lunaredu::audio {
dsp::Diagnostics dual_i2s_dsp_diagnostics(){
  dsp::Diagnostics d;d.input_peak=0.5f;d.output_peak=0.25f;d.rms=0.125f;
  d.limiter_gain=0.75f;d.limiter_events=7;d.soft_clip_samples=8;return d;
}
DualI2sDiagnostics dual_i2s_diagnostics(){
  DualI2sDiagnostics d;d.timeouts_left=2;d.timeouts_right=3;
  d.short_writes_left=4;d.short_writes_right=5;d.maximum_write_gap_ms=6;return d;
}
}
namespace lunaredu::system {
unsigned reads=0;
MemoryPoolFreeSnapshot read_memory_pool_free_snapshot(MemoryPool kind){
  assert(kind==MemoryPool::Dma);++reads;return {800,600,700};
}
}
int main(){
  audio::AudioHistory history;
  struct Output {int before=123;audio::AudioHistoryFrame frames[24];int after=456;} output;
  uint32_t latest=999;
  assert(history.read_recent(output.frames,24,latest)==0&&latest==0);
  assert(!history.due(499));history.capture(499,50);assert(system::reads==0);
  for(unsigned i=1;i<=120;++i)history.capture(i*500,int(i));
  const auto count=history.read_recent(output.frames,24,latest);
  assert(count==24&&latest==120&&output.before==123&&output.after==456);
  for(unsigned i=0;i<24;++i){
    const auto &f=output.frames[i];assert(f.sequence==97+i&&f.volume==int(f.sequence));
    assert(f.timestamp_ms==f.sequence*500&&f.peak_in==16383&&f.peak_out==8191);
    assert(f.limiter_gain_percent==75&&f.i2s_write_errors==5&&f.i2s_short_writes==9);
    assert(f.dma_free_bytes==800&&f.dma_largest_block==700&&f.dma_minimum_free_bytes==600);
  }
  history.capture(60001,3);assert(system::reads==120);
  assert(history.read_recent(nullptr,24,latest)==0&&latest==120);
  std::atomic<bool> done{false};
  std::thread producer([&]{for(unsigned i=121;i<=1120;++i)history.capture(i*500,int(i));done=true;});
  while(!done.load()){
    auto n=history.read_recent(output.frames,24,latest);
    for(size_t i=0;i<n;++i)assert(output.frames[i].sequence==latest-uint32_t(n-1-i));
  }
  producer.join();assert(system::reads==1120);
  audio::AudioHistory wrap;wrap.capture(UINT32_MAX-100,1);
  assert(!wrap.due(300)&&wrap.due(400));wrap.capture(400,2);
  assert(wrap.read_recent(output.frames,24,latest)==2&&latest==2);
  std::cout<<"Audio history: 96-frame retention, newest-page bounds, cadence/wrap, shared heap facts and concurrent read coherence passed\n";
}
