#include "lunaros_audio_resampler.h"
#include "rate_cvt_test_control.h"
#include <cassert>
#include <array>
#include <cstdio>
using namespace lunaredu::audio;
int main() {
  {
    PcmRateConverter c;
    size_t written=99;int16_t input[2]{10,20},output[12]{};
    assert(!c.process(input,2,output,12,written)&&written==0);
    assert(c.configure(8000,48000));
    assert(!c.configure(8001,24000));
    assert(c.maximum_output_samples(2)==12);
    const auto calls=rate_cvt_test::calls;
    assert(!c.process(input,2,output,11,written));
    assert(!c.process(input,2,input,12,written));
    assert(rate_cvt_test::calls==calls); // Reject capacity before consuming.
    assert(c.process(input,2,output,12,written)&&written==12);
    assert(rate_cvt_test::calls==calls+2&&input[0]==10&&input[1]==20);
    rate_cvt_test::fail_open=true;
    assert(!c.configure(24000,16000));
    rate_cvt_test::fail_open=false;
    assert(c.maximum_output_samples(2)==12); // Failed change keeps old owner.
    rate_cvt_test::fail_reset=true;assert(!c.reset());rate_cvt_test::fail_reset=false;
    assert(c.reset());
    rate_cvt_test::fail_process=true;
    assert(!c.process(input,2,output,12,written)&&written==0);
    rate_cvt_test::fail_process=false;
    assert(c.configure(24000,24000));
    assert(c.process(input,2,output,12,written)&&written==2&&output[1]==20);
    assert(c.process(input,2,input,2,written)&&written==2&&input[1]==20);
  }
  {
    PcmRateConverter c;int16_t output[6],input=25;size_t written=0;
    assert(c.configure(8000,48000)&&c.process(&input,1,output,6,written)&&written==6);
    c.close();
    assert(!c.process(&input,1,output,6,written)&&written==0);
    assert(c.configure(24000,24000)&&c.process(&input,1,output,6,written)&&written==1&&output[0]==input);
  }
  assert(rate_cvt_test::opens==rate_cvt_test::closes);
  puts("PASS official rate adapter: capacity, fixed quantum, failure, reset, bypass and handle ownership");
}
