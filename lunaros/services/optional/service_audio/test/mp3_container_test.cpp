#include "lunaros_audio_mp3.h"
#include <algorithm>
#include <vector>
#include <cstring>
#include <cassert>
#include <iostream>
int main(){
  using namespace lunaredu::audio;
  for(size_t chunk:{size_t(1),size_t(3),size_t(19),size_t(1024)}){
    Mp3StreamValidator validator;
    std::vector<uint8_t> file={'I','D','3',4,0,0,0,0,0,7,1,2,3,4,5,6,7};
    for(unsigned i=0;i<2;++i){const size_t start=file.size();file.resize(start+417);file[start]=0xff;file[start+1]=0xfb;file[start+2]=0x90;}
    const auto end=file.size();file.resize(end+128);std::memcpy(file.data()+end,"TAG",3);
    for(size_t i=0;i<file.size();i+=chunk)assert(validator.push(file.data()+i,std::min(chunk,file.size()-i)));
    assert(validator.finish());
  }
  for(unsigned mode=0;mode<3;++mode){
    Mp3StreamValidator validator;std::vector<uint8_t> file(417,0);
    file[0]=0xff;file[1]=0xfb;file[2]=0x90;
    if(mode==0)file.pop_back();
    if(mode==1)file[2]=0; // Free format is outside the import contract.
    if(mode==2)file[1]=0xfd; // Layer II is not MP3.
    assert(!validator.push(file.data(),file.size())||!validator.finish());
  }
  std::cout<<"PASS structural MP3 admission: frame boundaries, ID3 prefix/footer, truncation and layer rejection; no decoding claim\n";
}
