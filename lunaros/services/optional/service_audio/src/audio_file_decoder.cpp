#include "lunaros_audio_file_decoder.h"
#include <algorithm>
#include <cstring>
#include <mutex>
#include <esp_audio_simple_dec.h>
#include <esp_audio_dec_reg.h>
#include <impl/esp_wav_dec.h>
#ifndef LUNAROS_AUDIO_MP3_CODEC
#define LUNAROS_AUDIO_MP3_CODEC 0
#endif
#if LUNAROS_AUDIO_MP3_CODEC
#include <esp_mp3_dec.h>
#endif

namespace lunaredu::audio {
namespace {
std::mutex registry_mutex;
size_t wave_users=0,mp3_users=0;
bool owns_wave=false,owns_mp3=false;
bool acquire_decoder(AudioFileFormat format) {
  std::lock_guard lock(registry_mutex);
  if(format==AudioFileFormat::Wave){
    if(!wave_users&&esp_audio_simple_check_audio_type(ESP_AUDIO_SIMPLE_DEC_TYPE_WAV)!=ESP_AUDIO_ERR_OK){
      if(esp_wav_dec_register()!=ESP_AUDIO_ERR_OK)return false;
      owns_wave=true;
    }
    ++wave_users;return true;
  }
#if LUNAROS_AUDIO_MP3_CODEC
  if(!mp3_users&&!esp_audio_dec_get_ops(ESP_AUDIO_TYPE_MP3)){
    if(esp_mp3_dec_register()!=ESP_AUDIO_ERR_OK)return false;
    owns_mp3=true;
  }
  ++mp3_users;return true;
#else
  return false;
#endif
}
void release_decoder(AudioFileFormat format) {
  std::lock_guard lock(registry_mutex);
  if(format==AudioFileFormat::Wave){
    if(wave_users&&!--wave_users&&owns_wave){(void)esp_wav_dec_unregister();owns_wave=false;}
  }else if(mp3_users&&!--mp3_users&&owns_mp3){
    esp_audio_dec_unregister(ESP_AUDIO_TYPE_MP3);owns_mp3=false;
  }
}
uint32_t u32(const uint8_t *p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
}
AudioFileDecoder::~AudioFileDecoder(){close();}
void AudioFileDecoder::close(){
  if(codec_)esp_audio_simple_dec_close(codec_);
  if(registered_)release_decoder(format_);
  codec_=nullptr;registered_=false;writer_=nullptr;context_=nullptr;finished_=true;
}
bool AudioFileDecoder::supported(AudioFileFormat format){
  return format==AudioFileFormat::Wave||(format==AudioFileFormat::Mp3&&bool(LUNAROS_AUDIO_MP3_CODEC));
}
bool AudioFileDecoder::fail(std::string &error,const char *message){failed_=true;error=message;return false;}
bool AudioFileDecoder::open(AudioFileFormat format,DecodedAudioWriter writer,void *context,std::string &error){
  if(!supported(format))return fail(error,"audio_format_unsupported");
  if(writer_||!writer||failed_||finished_)return fail(error,"audio_decoder_configuration_invalid");
  format_=format;
  if(!acquire_decoder(format))return fail(error,"audio_decoder_register_failed");
  registered_=true;
  esp_audio_simple_dec_cfg_t config{};
  config.dec_type=format==AudioFileFormat::Wave?ESP_AUDIO_SIMPLE_DEC_TYPE_WAV:ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
  config.use_frame_dec=false;
  if(esp_audio_simple_dec_open(&config,&codec_)!=ESP_AUDIO_ERR_OK)return fail(error,"audio_decoder_open_failed");
  writer_=writer;context_=context;error.clear();return true;
}
bool AudioFileDecoder::push(const uint8_t *data,size_t size,std::string &error){
  if(failed_||finished_||!writer_||(!data&&size))return fail(error,"audio_decoder_invalid");
  if(size>kMaximumFileBytes-total_)return fail(error,"audio_file_too_large");
  total_+=size;
  if(format_==AudioFileFormat::Mp3&&!mp3_validator_.push(data,size))return fail(error,"audio_mp3_invalid");
  const auto prefix=std::min(size,header_.size()-header_used_);
  if(prefix){std::memcpy(header_.data()+header_used_,data,prefix);header_used_+=prefix;}
  if(header_used_==header_.size()&&!header_checked_){
    header_checked_=true;
    if(format_==AudioFileFormat::Wave){
      if(std::memcmp(header_.data(),"RIFF",4)||std::memcmp(header_.data()+8,"WAVE",4))return fail(error,"audio_wave_header_invalid");
      expected_=uint64_t(u32(header_.data()+4))+8;
      if(expected_<44||expected_>kMaximumFileBytes)return fail(error,"audio_wave_size_invalid");
    }else if(!std::memcmp(header_.data(),"ID3",3)){
      size_t bytes=0;
      if(!parse_id3v2_header(header_.data(),header_.size(),bytes)||bytes>65536)return fail(error,"audio_mp3_tag_invalid");
    }
  }
  if(expected_&&total_>expected_)return fail(error,"audio_wave_trailing_data");
  while(size){
    // Retain the last quantum so EOF reaches the official parser with the
    // final real bytes, even when file size is an exact quantum multiple.
    if(used_==input_.size()&&!process(false,error))return false;
    const auto take=std::min(size,input_.size()-used_);
    std::memcpy(input_.data()+used_,data,take);used_+=take;data+=take;size-=take;
  }
  return true;
}
bool AudioFileDecoder::process(bool eos,std::string &error){
  esp_audio_simple_dec_raw_t raw{};
  raw.buffer=input_.data();raw.len=uint32_t(used_);raw.eos=eos;
  // Fixed output capacity supports the admitted PCM16 mono/stereo formats.
  // Never obey an unbounded needed_size allocation from input metadata.
  unsigned iterations=0;
  while(raw.len){
    if(++iterations>4096)return fail(error,"audio_decoder_no_progress");
    esp_audio_simple_dec_out_t output{};
    output.buffer=reinterpret_cast<uint8_t*>(pcm_.data());output.len=sizeof(pcm_);
    raw.consumed=0;
    const auto result=esp_audio_simple_dec_process(codec_,&raw,&output);
    if(result!=ESP_AUDIO_ERR_OK||raw.consumed>raw.len||output.decoded_size>sizeof(pcm_))
      return fail(error,result==ESP_AUDIO_ERR_BUFF_NOT_ENOUGH?"audio_decoder_capacity_exceeded":"audio_decode_failed");
    if(output.decoded_size){
      esp_audio_simple_dec_info_t info{};
      if(esp_audio_simple_dec_get_info(codec_,&info)!=ESP_AUDIO_ERR_OK||info.bits_per_sample!=16||
          info.channel<1||info.channel>2||info.sample_rate<8000||info.sample_rate>48000||
          output.decoded_size%(info.channel*sizeof(int16_t)))return fail(error,"audio_format_unsupported");
      const auto frames=output.decoded_size/(info.channel*sizeof(int16_t));
      if(!writer_(context_,pcm_.data(),frames,info.sample_rate,info.channel))return fail(error,"audio_output_cancelled");
      frames_+=frames;
    }
    if(!raw.consumed&&!output.decoded_size)return fail(error,"audio_decoder_no_progress");
    raw.buffer+=raw.consumed;raw.len-=raw.consumed;
  }
  used_=0;return true;
}
bool AudioFileDecoder::finish(std::string &error){
  if(failed_||finished_||!writer_)return false;
  if(!header_checked_||(format_==AudioFileFormat::Wave&&total_!=expected_)||
      (format_==AudioFileFormat::Mp3&&!mp3_validator_.finish()))return fail(error,"audio_file_truncated");
  if(!process(true,error))return false;
  if(!frames_)return fail(error,"audio_file_truncated");
  finished_=true;error.clear();return true;
}
}
