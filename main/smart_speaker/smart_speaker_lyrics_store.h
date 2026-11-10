#pragma once
#include <cstdio>
#include <memory>
#include <sys/stat.h>
#include "smart_speaker_lyrics.h"

namespace smart_speaker::lyrics {
inline std::string path_for(const std::string &track) {
  return track.substr(0,track.find_last_of('.'))+".lrc";
}
inline bool exists(const std::string &path) { struct stat s{}; return stat(path.c_str(),&s)==0; }
// FAT cannot replace an existing destination with rename. Keep the previous
// version until the new one has closed successfully; recover after power loss.
inline std::string readable(const std::string &path) {
  return exists(path)?path:path+".bak";
}
inline bool save(const std::string &path, const std::string &text) {
  const auto temp=path+".upload", backup=path+".bak";
  FILE *f=fopen(temp.c_str(),"wb"); if (!f) return false;
  bool ok=fwrite(text.data(),1,text.size(),f)==text.size();
  if (fclose(f)!=0) ok=false;
  if (!ok) { remove(temp.c_str()); return false; }
  const bool had=exists(path);
  if (had) { remove(backup.c_str()); if (rename(path.c_str(),backup.c_str())!=0) { remove(temp.c_str()); return false; } }
  if (rename(temp.c_str(),path.c_str())!=0) {
    if (had) rename(backup.c_str(),path.c_str());
    remove(temp.c_str()); return false;
  }
  remove(backup.c_str()); return true;
}
inline std::shared_ptr<const Document> load(const std::string &track) {
  const auto path=readable(path_for(track));
  struct stat s{};
  if (stat(path.c_str(),&s)!=0 || s.st_size<=0 || size_t(s.st_size)>kMaxBytes) return {};
  FILE *f=fopen(path.c_str(),"rb"); if (!f) return {};
  std::string text(size_t(s.st_size),'\0');
  bool ok=fread(text.data(),1,text.size(),f)==text.size(); fclose(f);
  auto doc=std::make_shared<Document>(); std::string error;
  if (!ok || !parse(text,*doc,error)) return {};
  return doc;
}
inline int delay(const std::string &track) {
  const auto path=readable(path_for(track)+".offset");
  FILE *f=fopen(path.c_str(),"rb"); if (!f) return 0;
  int ms=0; const bool ok=fscanf(f,"%d",&ms)==1; fclose(f);
  return ok&&ms>=-10000&&ms<=10000?ms:0;
}
} // namespace smart_speaker::lyrics
