// Scripted official API seam: no container parsing/PCM decoding is simulated.
// Header types come from pinned esp_audio_codec; real grammar runs on ESP-IDF.
#include "lunaros_audio_file_decoder.h"
#include <esp_audio_simple_dec.h>
#include <esp_audio_dec_reg.h>
#include <impl/esp_wav_dec.h>
#include <cassert>
#include <algorithm>
#include <cstring>
#include <vector>
#include <cstdio>
namespace {
enum class Mode{Normal,NoProgress,Overconsume,Overproduce,Capacity,Unsupported,OpenFailure};
Mode mode=Mode::Normal;
bool registered=false,last_eos=false,cancel=false;
unsigned registrations=0,unregistrations=0,handles=0,calls=0;
size_t delivered=0;
std::vector<uint8_t> envelope(size_t size){
  std::vector<uint8_t> bytes(size,0);std::memcpy(bytes.data(),"RIFF",4);std::memcpy(bytes.data()+8,"WAVE",4);
  const auto length=uint32_t(size-8);for(unsigned i=0;i<4;++i)bytes[4+i]=uint8_t(length>>(8*i));
  return bytes;
}
bool sink(void *,const int16_t *,size_t frames,unsigned rate,unsigned channels){
  assert(rate==16000&&channels==1);delivered+=frames;return !cancel;
}
}
extern "C" {
esp_audio_err_t esp_audio_simple_check_audio_type(esp_audio_simple_dec_type_t){return registered?ESP_AUDIO_ERR_OK:ESP_AUDIO_ERR_NOT_SUPPORT;}
esp_audio_err_t esp_wav_dec_register(){registered=true;++registrations;return ESP_AUDIO_ERR_OK;}
esp_audio_err_t esp_wav_dec_unregister(){assert(handles==0);registered=false;++unregistrations;return ESP_AUDIO_ERR_OK;}
void esp_audio_dec_unregister(esp_audio_type_t){assert(false);}
esp_audio_err_t esp_audio_simple_dec_open(esp_audio_simple_dec_cfg_t *cfg,void **handle){
  assert(registered&&cfg->dec_type==ESP_AUDIO_SIMPLE_DEC_TYPE_WAV&&!cfg->use_frame_dec);
  if(mode==Mode::OpenFailure)return ESP_AUDIO_ERR_FAIL;
  ++handles;*handle=reinterpret_cast<void *>(uintptr_t(handles));return ESP_AUDIO_ERR_OK;
}
void esp_audio_simple_dec_close(void *){assert(handles);--handles;}
esp_audio_err_t esp_audio_simple_dec_process(void *,esp_audio_simple_dec_raw_t *raw,esp_audio_simple_dec_out_t *out){
  ++calls;last_eos=raw->eos;assert(raw->len<=2048&&out->len==4608);
  if(mode==Mode::NoProgress)return ESP_AUDIO_ERR_OK;
  if(mode==Mode::Capacity){out->needed_size=1000000;return ESP_AUDIO_ERR_BUFF_NOT_ENOUGH;}
  raw->consumed=mode==Mode::Overconsume?raw->len+1:raw->len;
  out->decoded_size=mode==Mode::Overproduce?out->len+1:4;
  std::memset(out->buffer,0,4);return ESP_AUDIO_ERR_OK;
}
esp_audio_err_t esp_audio_simple_dec_get_info(void *,esp_audio_simple_dec_info_t *info){
  info->sample_rate=16000;info->channel=1;info->bits_per_sample=mode==Mode::Unsupported?24:16;return ESP_AUDIO_ERR_OK;
}
}
int main(){
  using namespace lunaredu::audio;
  for(const auto bytes:{44u,2048u,4096u})for(const auto quantum:{1u,7u,2048u}){
    delivered=0;calls=0;last_eos=false;
    AudioFileDecoder decoder;std::string error;const auto file=envelope(bytes);
    assert(decoder.open(AudioFileFormat::Wave,sink,nullptr,error));
    for(size_t i=0;i<file.size();i+=quantum)assert(decoder.push(file.data()+i,std::min<size_t>(quantum,file.size()-i),error));
    assert(decoder.finish(error)&&last_eos&&delivered>0);
    decoder.close();assert(handles==0&&!registered);
  }
  // Overlapping sessions share only owned registration; close precedes unregister.
  {
    const auto before=registrations;AudioFileDecoder a,b;std::string error;
    assert(a.open(AudioFileFormat::Wave,sink,nullptr,error)&&b.open(AudioFileFormat::Wave,sink,nullptr,error));
    assert(registrations==before+1);a.close();assert(handles==1&&registered);b.close();assert(!registered);
  }
  registered=true;
  {AudioFileDecoder borrowed;std::string error;assert(borrowed.open(AudioFileFormat::Wave,sink,nullptr,error));}
  assert(registered);registered=false; // Borrowed registrations survive our owner.
  for(const auto failure:{Mode::NoProgress,Mode::Overconsume,Mode::Overproduce,Mode::Capacity,Mode::Unsupported,Mode::OpenFailure}){
    mode=failure;AudioFileDecoder decoder;std::string error;const auto file=envelope(44);
    const bool opened=decoder.open(AudioFileFormat::Wave,sink,nullptr,error);
    if(opened)assert(!decoder.push(file.data(),file.size(),error)||!decoder.finish(error));
    assert(!error.empty());decoder.close();assert(handles==0&&!registered);
  }
  mode=Mode::Normal;cancel=true;
  {AudioFileDecoder decoder;std::string error;const auto file=envelope(44);
   assert(decoder.open(AudioFileFormat::Wave,sink,nullptr,error));
   assert(decoder.push(file.data(),file.size(),error)&&!decoder.finish(error)&&error=="audio_output_cancelled");}
  assert(handles==0&&registrations==unregistrations);
  puts("PASS simple decoder adapter: EOF quanta, owned/borrowed registry, bounded progress/capacity, format rejection and cancellation (scripted API only)");
}
