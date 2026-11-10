#include "lunaros_audio_pcm_output.h"
#include "lunaros_audio_resampler.h"
#include "realtime/gmf_pcm_pipeline.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <new>

namespace lunaredu::audio {
struct PcmOutputAdapter::Impl {
  PcmOutputPorts ports;
  GmfPcmPipeline pipeline;
  PcmRateConverter converter;
  unsigned input_rate=0,input_channels=0,output_rate=0,output_channels=0;
  bool configured=false;
  const char *error="";
  std::array<int16_t,GmfPcmPipeline::kMaximumFrames*6> converted{};
  static GmfPcmPipeline::Result output(void *opaque,const int16_t *samples,size_t frames){
    auto &s=*static_cast<Impl *>(opaque);
    const int16_t *pcm=samples;
    size_t count=frames;
    if(s.input_rate!=s.output_rate){
      if(!s.converter.process(samples,frames,s.converted.data(),s.converted.size(),count)){
        s.error="audio_rate_converter_process_failed";return GmfPcmPipeline::Result::Failed;
      }
      pcm=s.converted.data();
    }
    for(size_t offset=0;offset<count;){
      const auto take=std::min<size_t>(256,count-offset);
      if(!s.ports.write(s.ports.context,pcm+offset*s.output_channels,take,s.output_rate,s.output_channels)){
        s.error="audio_output_write_failed";return GmfPcmPipeline::Result::Failed;
      }
      offset+=take;
    }
    return GmfPcmPipeline::Result::Written;
  }
};
PcmOutputAdapter::PcmOutputAdapter():impl_(new(std::nothrow) Impl){}
PcmOutputAdapter::~PcmOutputAdapter(){if(!close())std::abort();}
bool PcmOutputAdapter::configure(PcmOutputPorts ports){
  if(!impl_)impl_.reset(new(std::nothrow) Impl);
  if(!impl_||!ports.write||!valid_pcm_effects(ports.effects)||ports.channels>2||
      (ports.sample_rate&&(ports.sample_rate<8000||ports.sample_rate>48000))||!close())return false;
  impl_->ports=ports;impl_->configured=true;impl_->error="";return true;
}
bool PcmOutputAdapter::write(const int16_t *samples,size_t frames,unsigned rate,unsigned channels){
  if(!impl_||!impl_->configured||(!samples&&frames)||rate<8000||rate>48000||channels<1||channels>2||frames>SIZE_MAX/channels)return false;
  if(!frames)return true;
  auto &s=*impl_;
  s.error="";
  const unsigned output_rate=s.ports.sample_rate?s.ports.sample_rate:rate;
  const unsigned output_channels=s.ports.channels?s.ports.channels:channels;
  if(output_rate!=rate&&output_channels!=1){s.error="audio_rate_converter_channels_unsupported";return false;}
  if(s.input_rate!=rate||s.input_channels!=channels){
    if(!s.pipeline.close()){s.error="audio_pcm_retirement_pending";return false;}
    s.input_rate=s.input_channels=0;
    if(output_rate!=rate&&!s.converter.configure(rate,output_rate)){s.error="audio_rate_converter_open_failed";return false;}
    if(output_rate==rate)s.converter.close();
    if(s.ports.prepare&&!s.ports.prepare(s.ports.context,output_rate,output_channels)){s.error="audio_output_prepare_failed";return false;}
    s.output_rate=output_rate;s.output_channels=output_channels;
    if(!s.pipeline.open(rate,nullptr,Impl::output,&s,channels,output_channels,s.ports.effects)){s.error="audio_pcm_pipeline_open_failed";return false;}
    s.input_rate=rate;s.input_channels=channels;
  }
  for(size_t offset=0;offset<frames;){
    const auto take=std::min(GmfPcmPipeline::kMaximumFrames,frames-offset);
    if(s.pipeline.write(samples+offset*channels,take)!=GmfPcmPipeline::Result::Written){
      if(!*s.error)s.error="audio_pcm_pipeline_failed";
      s.input_rate=s.input_channels=0;return false; // Rebuild a sticky failed stream before the next source block.
    }
    offset+=take;
  }
  return true;
}
const char *PcmOutputAdapter::error()const{return impl_?impl_->error:"audio_pcm_memory_low";}
bool PcmOutputAdapter::close(){
  if(!impl_)return true;
  auto &s=*impl_;
  if(!s.pipeline.close())return false;
  s.converter.close();
  s.input_rate=s.input_channels=s.output_rate=s.output_channels=0;
  s.ports={};s.configured=false;return true;
}
bool PcmOutputAdapter::write_bytes(const uint8_t *bytes,size_t size,unsigned rate,unsigned channels){
  if(!impl_||!impl_->configured||rate<8000||rate>48000||channels<1||channels>2||(!bytes&&size)||size%(channels*sizeof(int16_t)))return false;
  std::array<int16_t,GmfPcmPipeline::kMaximumFrames*2> aligned;
  const size_t frame_bytes=channels*sizeof(int16_t);
  while(size){
    const auto frames=std::min(GmfPcmPipeline::kMaximumFrames,size/frame_bytes);
    const auto count=frames*frame_bytes;
    std::memcpy(aligned.data(),bytes,count);
    if(!write(aligned.data(),frames,rate,channels))return false;
    bytes+=count;size-=count;
  }
  return true;
}
}
