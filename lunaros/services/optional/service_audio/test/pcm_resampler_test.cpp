#include "lunaros_audio_resampler.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cstdio>
using lunaredu::audio::PcmRateConverter;
size_t convert_sample(PcmRateConverter &converter,int16_t sample,int16_t (&output)[6]) {
  size_t written=0;assert(converter.process(&sample,1,output,6,written));return written;
}
std::vector<int16_t> tone(unsigned rate,unsigned target,double hz) {
  PcmRateConverter r;assert(r.configure(rate,target));std::vector<int16_t> result;
  for(unsigned i=0;i<rate;++i) {
    int16_t values[6];
    const auto count=convert_sample(r,int16_t(12000*std::sin(6.283185307179586*hz*i/rate)),values);
    result.insert(result.end(),values,values+count);
  }
  assert(result.size()==target);return result;
}
double rms(const std::vector<int16_t> &data) {
  const size_t settling=data.size()/10; // Compare steady-state gain/alias bands.
  double sum=0;for(size_t i=settling;i<data.size();++i)sum+=double(data[i])*data[i];
  return std::sqrt(sum/(data.size()-settling));
}
int main() {
  PcmRateConverter r;int16_t values[6],sample=1;size_t written=0;
  assert(!r.configure(0,24000)&&!r.process(&sample,1,values,6,written));
  assert(!r.configure(8001,24000));
  for(auto pair:{std::pair{48000u,24000u},std::pair{44100u,24000u},std::pair{8000u,48000u},std::pair{24000u,24000u}}) {
    assert(r.configure(pair.first,pair.second));size_t count=0;
    int dc_error=0;
    for(unsigned i=0;i<pair.first;++i){const auto n=convert_sample(r,10000,values);count+=n;if(i>pair.first/10)for(size_t j=0;j<n;++j)dc_error=std::max(dc_error,std::abs(values[j]-10000));}
    std::printf("PCM DC %u -> %u maximum_error=%d\n",pair.first,pair.second,dc_error);
    assert(dc_error<=4); // Fixed-point vendor kernel rounding, <= -78 dBFS.
    assert(count==pair.second);
  }
  const auto pass=rms(tone(48000,24000,1000)),alias=rms(tone(48000,24000,19000));
  assert(pass>8000&&pass<9000&&alias<pass/100);
  const auto irrational=tone(44100,24000,1000);assert(rms(irrational)>8000);
  // Arbitrary input block boundaries must not reset phase/history.
  PcmRateConverter a,b;assert(a.configure(44100,24000)&&b.configure(44100,24000));
  for(unsigned block=0;block<25;++block)for(unsigned i=0;i<137;++i) {
    int16_t av[6],bv[6];const auto sample=int16_t((block*137+i)%30000);
    const auto na=convert_sample(a,sample,av),nb=convert_sample(b,sample,bv);assert(na==nb);
    for(size_t j=0;j<na;++j)assert(av[j]==bv[j]);
  }
  std::cout<<"PASS PCM rates, DC, continuity and alias rejection ("<<20*std::log10(pass/alias)<<" dB)\n";
  return 0;
}
