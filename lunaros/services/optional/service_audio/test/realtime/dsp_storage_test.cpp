#define ESP_PLATFORM 1
#include "realtime/realtime_dsp_storage.h"
#include <cassert>
#include <cstdlib>
#include <iostream>

namespace {
bool fail = false;
int fail_after=-1;
unsigned allocated = 0, freed = 0,filter_bytes=0;
}
void *heap_caps_malloc(size_t bytes, unsigned caps) {
  assert(bytes>0 && bytes<=1024);
  assert(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (fail || fail_after==0) return nullptr;
  if(fail_after>0)--fail_after;
  if(bytes!=sizeof(lunaredu::audio::dsp::AudioProcessor))filter_bytes=unsigned(bytes);
  ++allocated;
  return std::malloc(bytes);
}
void heap_caps_free(void *pointer) { ++freed; std::free(pointer); }
int main() {
  using namespace lunaredu::audio;
  fail = true;
  assert(!internal::make_realtime_dsp());
  assert(allocated == 0 && freed == 0);
  fail = false;
  // Parent state is constructed, but its hot filter bank cannot be admitted.
  fail_after=1;
  {
    auto state=internal::make_realtime_dsp();
    assert(state && !state->configure(16000,dsp::preset_settings(dsp::Preset::kFlat)));
    float left=1,right=-1;state->process_filters(left,right);assert(left==0&&right==0);
    state->reset();(void)state->high_pass_coefficients();
  }
  assert(allocated==1 && freed==1);fail_after=-1;
  for (unsigned cycle = 0; cycle < 100; ++cycle) {
    auto state = internal::make_realtime_dsp();
    assert(state && state->configure(16000, dsp::preset_settings(dsp::Preset::kFlat)));
    dsp::AudioProcessor reference;
    assert(reference.configure(16000, dsp::preset_settings(dsp::Preset::kFlat)));
    state->request_unmute();reference.request_unmute();
    for (int sample = -32768; sample <= 32767; ++sample) {
      float a,b,c,x,y,z;
      state->process_common(sample,sample,a,b,c);
      reference.process_common(sample,sample,x,y,z);
      state->finalize_output(a,b);reference.finalize_output(x,y);
      assert(a==x && b==y && c==z);
      assert(state->quantize(a)==reference.quantize(x));
    }
    auto moved = std::move(state);
    assert(!state && moved);
  }
  assert(allocated == 301 && freed == 301);
  std::cout << "Internal DSP ownership / failure / full-range sample equality passed; bytes="
            << sizeof(dsp::AudioProcessor) << " filters=" << filter_bytes << '\n';
}
