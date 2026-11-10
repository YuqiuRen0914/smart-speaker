// Owner-only seam for fixed 44-byte PCM16 fixtures in file_player_test and
// Music's player_test. This is not a WAV parser or codec substitute: arbitrary
// containers and waveform correctness are exercised by the real IDF fixture.
#include "lunaros_audio_file_decoder.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <new>
namespace lunaredu::audio {
namespace {
struct Fixture {
  std::array<uint8_t,44> header{};
  std::array<uint8_t,512> bytes{};
  size_t prefix=0,used=0,total=0,expected=0;
  unsigned channels=0,rate=0;
};
uint32_t number(const uint8_t *p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
}
AudioFileDecoder::~AudioFileDecoder(){close();}
void AudioFileDecoder::close(){delete static_cast<Fixture *>(codec_);codec_=nullptr;writer_=nullptr;finished_=true;}
bool AudioFileDecoder::supported(AudioFileFormat format){return format==AudioFileFormat::Wave;}
bool AudioFileDecoder::fail(std::string &error,const char *message){failed_=true;error=message;return false;}
bool AudioFileDecoder::open(AudioFileFormat format,DecodedAudioWriter writer,void *context,std::string &error){
  if(!supported(format))return fail(error,"audio_format_unsupported");
  if(codec_||!writer||finished_)return fail(error,"audio_decoder_configuration_invalid");
  codec_=new(std::nothrow)Fixture;
  if(!codec_)return fail(error,"fixture_allocation_failed");
  writer_=writer;context_=context;error.clear();return true;
}
bool AudioFileDecoder::push(const uint8_t *data,size_t size,std::string &error){
  if(!codec_||failed_||finished_)return false;
  auto &s=*static_cast<Fixture *>(codec_);s.total+=size;
  while(size&&s.prefix<s.header.size()){
    s.header[s.prefix++]=*data++;--size;
    if(s.prefix==s.header.size()){
      assert(!std::memcmp(s.header.data(),"RIFF",4)&&!std::memcmp(s.header.data()+8,"WAVEfmt ",8));
      assert(number(s.header.data()+16)==16&&s.header[20]==1&&s.header[34]==16);
      assert(!std::memcmp(s.header.data()+36,"data",4));
      s.channels=s.header[22];s.rate=number(s.header.data()+24);s.expected=number(s.header.data()+4)+8;
      assert((s.channels==1||s.channels==2)&&s.rate==16000);
    }
  }
  while(size){
    const auto take=std::min(size,s.bytes.size()-s.used);
    std::memcpy(s.bytes.data()+s.used,data,take);s.used+=take;data+=take;size-=take;
    const auto frame_bytes=s.channels*2;
    const auto complete=s.used/frame_bytes*frame_bytes;
    for(size_t i=0;i<complete/2;++i)pcm_[i]=int16_t(uint16_t(s.bytes[i*2])|uint16_t(s.bytes[i*2+1])<<8);
    if(complete&&!writer_(context_,pcm_.data(),complete/frame_bytes,s.rate,s.channels))return fail(error,"audio_output_cancelled");
    s.used-=complete;std::memmove(s.bytes.data(),s.bytes.data()+complete,s.used);
  }
  return true;
}
bool AudioFileDecoder::finish(std::string &error){
  if(!codec_||failed_||finished_)return false;
  const auto &s=*static_cast<Fixture *>(codec_);
  if(s.total!=s.expected||s.used)return fail(error,"fixture_truncated");
  finished_=true;error.clear();return true;
}
}
