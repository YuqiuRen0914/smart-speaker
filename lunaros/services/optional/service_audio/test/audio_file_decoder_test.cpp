#include "lunaros_audio_file_decoder.h"
#include <vector>
#include <cstring>
#include <cassert>
#include <iostream>
using namespace lunaredu::audio;
namespace {
std::vector<uint8_t> wave() {
  std::vector<uint8_t> v={'R','I','F','F',42,0,0,0,'W','A','V','E',
    'J','U','N','K',1,0,0,0,99,0,'f','m','t',' ',16,0,0,0,
    1,0,1,0,0x80,0x3e,0,0,0,0x7d,0,0,2,0,16,0,'d','a','t','a',4,0,0,0,0x34,0x12,0,0x80};
  v[4]=uint8_t(v.size()-8);return v;
}
struct Sink {std::vector<int16_t> samples;bool cancel=false;};
bool write(void *p,const int16_t *s,size_t n,unsigned rate,unsigned channels) {
  auto &sink=*static_cast<Sink *>(p);assert(rate==16000&&channels==1);
  sink.samples.insert(sink.samples.end(),s,s+n);return !sink.cancel;
}
}
int main() {
  for(size_t chunk:{size_t(1),size_t(2),size_t(3),size_t(7),size_t(1024)}) {
    AudioFileDecoder decoder;Sink sink;std::string error;const auto file=wave();
    assert(decoder.open(AudioFileFormat::Wave,write,&sink,error));
    for(size_t i=0;i<file.size();i+=chunk)assert(decoder.push(file.data()+i,std::min(chunk,file.size()-i),error));
    assert(decoder.finish(error)&&sink.samples==std::vector<int16_t>({0x1234,-32768}));
  }
  for(unsigned mode=0;mode<5;++mode) {
    AudioFileDecoder decoder;Sink sink;std::string error;auto file=wave();
    if(mode==0)file.pop_back();
    if(mode==1)file[44]=8; // unsupported PCM bits
    if(mode==2)file[4]=20; // data lies outside RIFF
    if(mode==3)file[50]=3; // partial PCM frame
    if(mode==4)sink.cancel=true;
    assert(decoder.open(AudioFileFormat::Wave,write,&sink,error));
    const bool pushed=decoder.push(file.data(),file.size(),error);
    assert(!pushed||!decoder.finish(error));assert(!error.empty());
  }
  AudioFileDecoder disabled;Sink sink;std::string e;
  const auto unsupported=static_cast<AudioFileFormat>(999);
  assert(!AudioFileDecoder::supported(unsupported));
  assert(!disabled.open(unsupported,write,&sink,e)&&e=="audio_format_unsupported");
  std::cout<<"PASS streaming WAV odd chunks, arbitrary boundaries, signed PCM, truncation, unsupported format and cancellation\n";
  return 0;
}
