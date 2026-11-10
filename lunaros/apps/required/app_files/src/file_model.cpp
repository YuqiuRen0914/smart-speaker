#include "lunaros_files_model.h"
#include <algorithm>
#include <cctype>
namespace lunaredu::desktop {
bool file_user_path(const std::string &p) {
  if(p.empty() || p.size()>255 || p[0]!='/' || p.back()=='/' || p.find('\\')!=std::string::npos ||
      p.find('\0')!=std::string::npos || p.find("//")!=std::string::npos) return false;
  for(size_t start=1;start<p.size();) {
    const auto end=p.find('/',start);const auto part=p.substr(start,end-start);
    if(part=="." || part=="..") return false;
    start=end==std::string::npos?p.size():end+1;
  }
  return p=="/scripts" || p.compare(0,9,"/scripts/")==0 || p=="/performances" || p.compare(0,14,"/performances/")==0 || p=="/saves" || p.compare(0,7,"/saves/")==0;
}
const char *file_name(const std::string &path) { const auto slash=path.find_last_of('/');return path.c_str()+(slash==std::string::npos?0:slash+1); }
FileKind file_kind(const std::string &path,bool directory) {
  if(directory) return FileKind::Directory;
  auto extension=path.substr(path.find_last_of('.')==std::string::npos?path.size():path.find_last_of('.'));
  std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
  if(extension==".txt" || extension==".md" || extension==".py" || extension==".json" || extension==".csv") return FileKind::Text;
  if(extension==".jpg" || extension==".jpeg") return FileKind::Jpeg;
  if(extension==".wav" || extension==".mp3") return FileKind::Audio;
  if(extension==".mjpeg" || extension==".mjpg" || extension==".mp4" || extension==".webm" || extension==".avi") return FileKind::Video;
  return FileKind::Binary;
}
unsigned file_rows(const FileBrowserState &s) {
  if(!s.content || !s.content->available || !s.content->message.empty()) return 1;
  if(s.location.view==FileView::Sort) return 2;
  if(s.location.view!=FileView::Directory) return 1;
  return unsigned(s.content->entries.size())+(s.location.page?1:0)+(s.content->more?1:0)+1;
}
}
