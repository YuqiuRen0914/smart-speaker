// Owner-only host seam. This does not simulate GMF: the ESP-IDF fixture links
// the production adapter and real vendor implementation for pipeline evidence.
#include "realtime/gmf_pcm_pipeline.h"
#include <algorithm>
#include <new>
namespace lunaredu::audio {
namespace {
struct State { GmfPcmPipeline::Transform transform; GmfPcmPipeline::Writer writer; void *context; unsigned input,output; };
}
GmfPcmPipeline::~GmfPcmPipeline(){close();}
bool GmfPcmPipeline::open(uint32_t rate,Transform transform,Writer writer,void *context,unsigned input,unsigned output,PcmEffects effects){
  if(!valid_pcm_effects(effects))return false;
  if(!output)output=input;
  close();if(!rate||!writer||input<1||input>2||output<1||output>2||(transform&&input!=output))return false;
  render_=new(std::nothrow)State{transform,writer,context,input,output};stream_=render_;return is_open();
}
GmfPcmPipeline::Result GmfPcmPipeline::write(const int16_t *samples,size_t frames){
  if(!stream_||!frames||frames>kMaximumFrames)return Result::Failed;
  auto &state=*static_cast<State *>(render_);
  if(samples)std::copy_n(samples,frames*state.input,input_.begin());else std::fill_n(input_.begin(),frames*state.input,int16_t(0));
  std::array<int16_t,kMaximumFrames*2> output{};
  if(state.transform){
    if(!state.transform(state.context,input_.data(),output.data(),frames))return Result::Failed;
  }else if(state.input==state.output)std::copy_n(input_.begin(),frames*state.input,output.begin());
  else for(size_t i=0;i<frames;++i){
    if(state.input==1)output[i*2]=output[i*2+1]=input_[i];
    else output[i]=int16_t((int(input_[i*2])+input_[i*2+1])/2);
  }
  return state.writer(state.context,output.data(),frames);
}
bool GmfPcmPipeline::close(){delete static_cast<State *>(render_);render_=stream_=nullptr;return true;}
}
