#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace lunaredu::audio {
struct Mp3FrameHeader { size_t bytes=0; unsigned rate=0; };
// Shared by probing, import and decoding. Only MPEG Layer III is supported.
inline bool parse_mp3_frame_header(const uint8_t *p,size_t size,Mp3FrameHeader &out) {
  if(!p||size<4)return false;
  const unsigned version=(p[1]>>3)&3,layer=(p[1]>>1)&3;
  const unsigned bitrate=(p[2]>>4)&15,frequency=(p[2]>>2)&3;
  if(p[0]!=0xff||(p[1]&0xe0)!=0xe0||version==1||layer!=1||!bitrate||bitrate==15||frequency==3)return false;
  constexpr unsigned rates[]={44100,48000,32000};
  constexpr unsigned mpeg1[]={0,32,40,48,56,64,80,96,112,128,160,192,224,256,320};
  constexpr unsigned mpeg2[]={0,8,16,24,32,40,48,56,64,80,96,112,128,144,160};
  out.rate=rates[frequency]/(version==3?1:version==2?2:4);
  out.bytes=(version==3?144000:72000)*(version==3?mpeg1[bitrate]:mpeg2[bitrate])/out.rate+((p[2]>>1)&1);
  return out.bytes>=4&&out.bytes<=4096;
}
inline bool parse_id3v2_header(const uint8_t *p,size_t size,size_t &bytes) {
  if(!p||size<10||p[0]!='I'||p[1]!='D'||p[2]!='3'||p[3]<2||p[3]>4||((p[6]|p[7]|p[8]|p[9])&0x80))return false;
  bytes=10+(size_t(p[6])<<21)+(size_t(p[7])<<14)+(size_t(p[8])<<7)+p[9];
  if(p[3]==4&&(p[5]&0x10))bytes+=10;
  return true;
}
// Bounded structural validation, without decoding, allocation or rewriting tags.
class Mp3StreamValidator {
public:
  bool push(const uint8_t *,size_t);
  bool finish() const;
private:
  enum class Stage:uint8_t {Prefix,Tag,Header,Frame,Tail,End,Failed};
  Stage stage_=Stage::Prefix;
  std::array<uint8_t,10> header_{};
  size_t used_=0,remaining_=0;
  bool frames_=false;
};
}
