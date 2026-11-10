#include "lunaros_audio_import.h"
#include "lunaros_audio_mp3.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>
using namespace lunaredu::audio;
struct Input {
  std::vector<uint8_t> bytes;
  size_t offset=0,chunk=2048,progress=0,fail_after=SIZE_MAX;
  bool oversize=false;
};
int read(void *context,uint8_t *p,size_t n) {
  auto &in=*static_cast<Input *>(context);
  if(in.oversize)return int(n+1);
  if(in.offset>=in.fail_after)return -1;
  n=std::min({n,in.chunk,in.bytes.size()-in.offset});
  std::memcpy(p,in.bytes.data()+in.offset,n);in.offset+=n;return int(n);
}
std::vector<uint8_t> frame() {
  std::vector<uint8_t> bytes(417,1);bytes[0]=0xff;bytes[1]=0xfb;bytes[2]=0x90;bytes[3]=0;
  return bytes;
}
bool run(Input &in,bool expected) {
  const std::string path="audio-import-test.tmp";
  AudioImportStream stream{&in,in.bytes.size(),read,[](void *p,size_t n){auto &in=*static_cast<Input *>(p);assert(n>in.progress);in.progress=n;}};
  size_t written=0;std::string error;
  const bool ok=import_mp3_file(stream,path,written,error);
  assert(ok==expected);
  FILE *file=std::fopen(path.c_str(),"rb");
  if(ok) {
    assert(error.empty()&&file&&written==in.bytes.size()&&in.progress==written);
    std::vector<uint8_t> actual(written);assert(std::fread(actual.data(),1,written,file)==written);
    assert(actual==in.bytes);std::fclose(file);std::remove(path.c_str());
  } else { assert(!error.empty()&&!file); }
  return ok;
}
int main() {
  const auto audio=frame();
  for(const size_t chunk:{size_t(1),size_t(3),size_t(8),size_t(10),size_t(128),size_t(2048)}) {
    for(const uint8_t version:{2,3,4}) {
      Input in;in.chunk=chunk;in.bytes.resize(42,0);std::memcpy(in.bytes.data(),"ID3",3);
      in.bytes[3]=version;in.bytes[9]=32;
      std::copy_n(audio.begin(),4,in.bytes.begin()+12); // A false sync inside metadata.
      if(version==4){in.bytes[5]=0x10;in.bytes.resize(52,0);std::memcpy(in.bytes.data()+42,"3DI",3);}
      in.bytes.insert(in.bytes.end(),audio.begin(),audio.end());
      in.bytes.insert(in.bytes.end(),audio.begin(),audio.end());
      const auto tail=in.bytes.size();in.bytes.resize(tail+128,0);std::memcpy(in.bytes.data()+tail,"TAG",3);
      run(in,true);
    }
    Input bare;bare.chunk=chunk;bare.bytes=audio;run(bare,true);
  }
  for(int mode=0;mode<8;++mode) {
    Input bad;bad.bytes=audio;
    if(mode==0)bad.bytes[1]=0xff; // Layer I.
    if(mode==1)bad.bytes[1]=0xfd; // Layer II.
    if(mode==2)bad.bytes.resize(100);
    if(mode==3)bad.bytes.push_back(0);
    if(mode==4){bad.bytes[2]=0;}
    if(mode==5){bad.fail_after=16;bad.chunk=8;}
    if(mode==6)bad.oversize=true;
    if(mode==7){bad.bytes.resize(10);std::memcpy(bad.bytes.data(),"ID3",3);bad.bytes[3]=5;}
    run(bad,false);
  }
  Input no_audio;no_audio.bytes={'I','D','3',3,0,0,0,0,0,0};run(no_audio,false);
  Input truncated_tag;truncated_tag.bytes={'I','D','3',3,0,0,0,0,0,127};run(truncated_tag,false);
  Input invalid_tag;invalid_tag.bytes={'I','D','3',3,0,0,128,0,0,0};run(invalid_tag,false);
  Input after_tail;after_tail.bytes=audio;auto end=after_tail.bytes.size();after_tail.bytes.resize(end+129,0);std::memcpy(after_tail.bytes.data()+end,"TAG",3);run(after_tail,false);
  Mp3FrameHeader header;assert(parse_mp3_frame_header(audio.data(),audio.size(),header)&&header.bytes==417&&header.rate==44100);
  std::cout<<"PASS shared MP3 import: byte-preserving ID3 v2/3/4 and v1, 1-2048 byte fragments, false sync, Layer I/II, truncation, malformed tags, read failures and partial cleanup\n";
}
