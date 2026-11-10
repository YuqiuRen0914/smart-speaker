#include "lunaros_audio_import.h"
#include "lunaros_audio_mp3.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace lunaredu::audio {
bool Mp3StreamValidator::push(const uint8_t *p,size_t n) {
  if(stage_==Stage::Failed||(!p&&n))return false;
  while(n) {
    if(stage_==Stage::End){stage_=Stage::Failed;return false;}
    if(stage_==Stage::Tag||stage_==Stage::Frame||stage_==Stage::Tail) {
      const size_t take=std::min(n,remaining_);p+=take;n-=take;remaining_-=take;
      if(remaining_)continue;
      if(stage_==Stage::Frame)frames_=true;
      stage_=stage_==Stage::Tail?Stage::End:Stage::Header;
      continue;
    }
    const size_t needed=stage_==Stage::Prefix?10:4;
    const size_t take=std::min(n,needed-used_);
    std::memcpy(header_.data()+used_,p,take);used_+=take;p+=take;n-=take;
    if(used_<needed)continue;
    used_=0;
    if(stage_==Stage::Prefix) {
      if(!std::memcmp(header_.data(),"ID3",3)) {
        size_t tag_bytes=0;
        if(!parse_id3v2_header(header_.data(),10,tag_bytes)){stage_=Stage::Failed;return false;}
        remaining_=tag_bytes-10;stage_=remaining_?Stage::Tag:Stage::Header;
      } else {
        stage_=Stage::Header;
        // A prefix without metadata is the first ten bytes of the MPEG stream.
        const auto prefix=header_;
        if(!push(prefix.data(),prefix.size()))return false;
      }
    } else if(!std::memcmp(header_.data(),"TAG",3)) {
      remaining_=124;stage_=Stage::Tail;
    } else {
      Mp3FrameHeader frame;
      if(!parse_mp3_frame_header(header_.data(),4,frame)){stage_=Stage::Failed;return false;}
      remaining_=frame.bytes-4;stage_=Stage::Frame;
    }
  }
  return true;
}
bool Mp3StreamValidator::finish() const {
  return frames_&&((stage_==Stage::Header&&used_==0)||stage_==Stage::End);
}
bool import_mp3_file(const AudioImportStream &stream,const std::string &path,
                     size_t &written,std::string &error) {
  written=0;error.clear();
  if(!stream.read||!stream.size){error="audio_import_empty";return false;}
  FILE *file=std::fopen(path.c_str(),"wb");
  if(!file){error="audio_import_open_failed";return false;}
  Mp3StreamValidator validator;
  std::array<uint8_t,2048> buffer;
  while(written<stream.size) {
    const size_t wanted=std::min(buffer.size(),stream.size-written);
    const int got=stream.read(stream.context,buffer.data(),wanted);
    if(got<=0||size_t(got)>wanted){error="audio_import_read_failed";break;}
    const auto count=size_t(got);
    if(!validator.push(buffer.data(),count)){error="audio_mp3_invalid";break;}
    if(std::fwrite(buffer.data(),1,count,file)!=count){error="audio_import_write_failed";break;}
    written+=count;
    if(stream.progress)stream.progress(stream.context,written);
  }
  if(error.empty()&&!validator.finish())error="audio_file_truncated";
  if(std::fclose(file)!=0&&error.empty())error="audio_import_close_failed";
  if(!error.empty()){std::remove(path.c_str());return false;}
  return true;
}
}
