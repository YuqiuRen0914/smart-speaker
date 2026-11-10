#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace lunaredu::audio {
struct AudioImportStream {
  void *context=nullptr;
  size_t size=0;
  int (*read)(void *,uint8_t *,size_t)=nullptr;
  void (*progress)(void *,size_t)=nullptr;
};
// Caller admits the destination and owns the final rename/library transaction.
// Uses a fixed 2 KiB buffer, preserves metadata, and removes partial output on
// read, validation, write or close failure. No task or decoder is started.
bool import_mp3_file(const AudioImportStream &,const std::string &temporary_path,
                     size_t &written,std::string &error);
}
