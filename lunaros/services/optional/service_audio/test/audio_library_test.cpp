#include "lunaros_audio_library.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace lunaredu::audio;
int main(int argc,char **argv){
  assert(argc==2);const auto root=std::filesystem::path(argv[1]);std::filesystem::create_directories(root);
  const auto put=[&](const char *name,std::initializer_list<unsigned char> bytes){std::ofstream f(root/name,std::ios::binary);for(auto c:bytes)f.put(char(c));};
  put("Stereo.MP3",{0xff,0xfb,0x90,0x00});put("tagged.mp3",{'I','D','3',4,0,0,0,0,0,0});
  put("bad.mp3",{'n','o','p','e'});put(".hidden.mp3",{0xff,0xfb,0x90,0});
  put("wrong-layer.mp3",{0xff,0xfd,0x90,0});
  put("wave.wav",{'R','I','F','F',0,0,0,0,'W','A','V','E'});
  std::filesystem::create_directories(root/"folder.mp3");
  std::vector<AudioLibraryEntry> entries;std::string error;
  assert(append_audio_directory(root.string().c_str(),AudioFileFormat::Mp3,entries,error));assert(entries.size()==2);
  for(const auto &e:entries){assert((e.size_bytes==4||e.size_bytes==10)&&e.format==AudioFileFormat::Mp3);assert(probe_audio_file(e.path.c_str(),e.format));}
  entries.clear();assert(append_audio_directory(root.string().c_str(),AudioFileFormat::Wave,entries,error));assert(entries.size()==1&&entries[0].name=="wave.wav");
  assert(!append_audio_directory(nullptr,AudioFileFormat::Mp3,entries,error));
  assert(!append_audio_directory((root/"missing").string().c_str(),AudioFileFormat::Mp3,entries,error));
  entries.clear();
  AudioLibraryScan scan({2,true,false},[](AudioLibraryEntry &&entry,void *context){
    static_cast<std::vector<AudioLibraryEntry> *>(context)->push_back(std::move(entry));return true;
  },&entries);
  assert(scan.offer("/music/hidden/.a.wav",20)&&scan.accepted()==0);
  assert(scan.offer("/music/folder.wav",0,true)&&scan.accepted()==0);
  assert(scan.offer("/music/a.mp3",10)&&scan.accepted()==0);
  assert(scan.offer("/music/a.WAV",10)&&scan.accepted()==1);
  assert(!scan.offer("/music/b.wav",11)&&scan.full()&&entries.size()==2);
  assert(!scan.offer("/music/c.wav",12)&&entries.size()==2);
  std::cout<<"PASS mounted audio library: case-insensitive names, signatures, layer validation, hidden files and directory rejection\n";
}
