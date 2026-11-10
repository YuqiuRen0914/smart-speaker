#include "lunaros_audio_library.h"
#include "lunaros_audio_mp3.h"
#include <array>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <string_view>
#include <utility>
namespace lunaredu::audio {
bool AudioLibraryScan::offer(const char *path,size_t bytes,bool directory){
  if(full()||!sink_)return false;
  if(!path||directory)return true;
  const std::string_view value(path);
  const auto slash=value.find_last_of('/');
  const auto name=value.substr(slash==std::string_view::npos?0:slash+1);
  if(name.empty()||name.front()=='.'||name.size()<4)return true;
  char suffix[4];for(size_t i=0;i<4;++i)suffix[i]=char(std::tolower(static_cast<unsigned char>(name[name.size()-4+i])));
  const bool mp3=std::string_view(suffix,4)==".mp3",wave=std::string_view(suffix,4)==".wav";
  if(!((mp3&&options_.mp3)||(wave&&options_.wave)))return true;
  AudioLibraryEntry entry{std::string(value),std::string(name),bytes,mp3?AudioFileFormat::Mp3:AudioFileFormat::Wave};
  if(sink_(std::move(entry),context_))++accepted_;
  return !full();
}
bool probe_audio_file(const char *path,AudioFileFormat format){
  if(!path)return false;
  FILE *file=std::fopen(path,"rb");if(!file)return false;
  std::array<uint8_t,12> h{};const auto size=std::fread(h.data(),1,h.size(),file);std::fclose(file);
  if(format==AudioFileFormat::Wave)return size==12&&h[0]=='R'&&h[1]=='I'&&h[2]=='F'&&h[3]=='F'&&h[8]=='W'&&h[9]=='A'&&h[10]=='V'&&h[11]=='E';
  size_t tag_bytes=0;
  if(size>=3&&h[0]=='I'&&h[1]=='D'&&h[2]=='3')return parse_id3v2_header(h.data(),size,tag_bytes);
  Mp3FrameHeader header;return parse_mp3_frame_header(h.data(),size,header);
}
bool append_audio_directory(const char *directory,AudioFileFormat format,std::vector<AudioLibraryEntry> &out,std::string &error,size_t maximum_entries){
  if(!directory){error="audio_library_directory_invalid";return false;}
  if(format!=AudioFileFormat::Wave&&format!=AudioFileFormat::Mp3){error="audio_format_unsupported";return false;}
  if(out.size()>=maximum_entries){error.clear();return true;}
  DIR *dir=opendir(directory);if(!dir){error="audio_library_open_failed";return false;}
  struct SinkContext {std::vector<AudioLibraryEntry> &out;AudioFileFormat format;} context{out,format};
  AudioLibraryScan scan({maximum_entries-out.size(),format==AudioFileFormat::Wave,format==AudioFileFormat::Mp3},
    [](AudioLibraryEntry &&entry,void *opaque){
      auto &sink=*static_cast<SinkContext *>(opaque);
      if(!probe_audio_file(entry.path.c_str(),sink.format))return false;
      sink.out.push_back(std::move(entry));return true;
    },&context);
  while(const auto *entry=readdir(dir)){
    const std::string path=std::string(directory)+"/"+entry->d_name;struct stat info{};
    if(stat(path.c_str(),&info)||!S_ISREG(info.st_mode))continue;
    if(!scan.offer(path.c_str(),size_t(std::max<off_t>(0,info.st_size))))break;
  }
  closedir(dir);error.clear();return true;
}
}
