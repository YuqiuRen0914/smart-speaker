#include "lunaros_audio_file_playback.h"
namespace lunaredu::audio {
bool AudioFilePlayback::open(AudioFileFormat format,PcmOutputPorts ports,std::string &error){
  if(opened_||!output_.configure(ports)){error="audio_playback_configuration_invalid";return false;}
  if(!decoder_.open(format,output,this,error))return false;
  opened_=true;return true;
}
bool AudioFilePlayback::output(void *opaque,const int16_t *pcm,size_t frames,unsigned rate,unsigned channels){
  return static_cast<AudioFilePlayback *>(opaque)->output_.write(pcm,frames,rate,channels);
}
bool AudioFilePlayback::push(const uint8_t *data,size_t size,std::string &error){
  if(!opened_){error="audio_playback_not_open";return false;}
  if(decoder_.push(data,size,error))return true;
  if(*output_.error())error=output_.error();
  return false;
}
bool AudioFilePlayback::finish(std::string &error){
  if(!opened_){error="audio_playback_not_open";return false;}
  if(decoder_.finish(error))return true;
  if(*output_.error())error=output_.error();
  return false;
}
bool AudioFilePlayback::close(){
  if(!output_.close())return false;
  decoder_.close();opened_=false;return true;
}
}
