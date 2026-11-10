#pragma once
#include "lunaros_audio_file_decoder.h"
#include <vector>
namespace lunaredu::audio {
struct AudioLibraryEntry {
  std::string path,name;
  size_t size_bytes=0;
  AudioFileFormat format=AudioFileFormat::Mp3;
};
struct AudioLibraryScanOptions {
  size_t maximum_entries=256;
  bool wave=true,mp3=true;
};
// Enumeration-independent admission. Backends retain mount, lease, recursive
// traversal and path authorization; sinks own accepted strings without a copy.
class AudioLibraryScan {
public:
  using Sink=bool (*)(AudioLibraryEntry &&,void *); // false rejects this entry
  AudioLibraryScan(AudioLibraryScanOptions options,Sink sink,void *context)
      :options_(options),sink_(sink),context_(context){}
  bool offer(const char *path,size_t bytes,bool directory=false);
  bool full()const{return accepted_>=options_.maximum_entries;}
  size_t accepted()const{return accepted_;}
private:
  AudioLibraryScanOptions options_;Sink sink_;void *context_;size_t accepted_=0;
};
// Local mounted filesystems, including the shared SD driver. Mount ownership
// remains with Storage/device_sdcard. Does not decode or allocate song data.
bool probe_audio_file(const char *,AudioFileFormat);
bool append_audio_directory(const char *,AudioFileFormat,std::vector<AudioLibraryEntry> &,std::string &,size_t maximum_entries=256);
}
