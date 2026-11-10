#include "desktop/ui/wallpaper.h"

namespace lunaredu::desktop {
bool valid_wallpaper_path(const std::string &path) {
  if(path.empty()||path.size()>255||path.front()!='/'||path.back()=='/'||path.find('\\')!=std::string::npos||path.find('|')!=std::string::npos||path.find("//")!=std::string::npos)return false;
  for(unsigned char c:path)if(c<32)return false;
  for(size_t start=1;start<path.size();){
    const auto end=path.find('/',start);const auto part=path.substr(start,end-start);
    if(part=="."||part=="..")return false;
    start=end==std::string::npos?path.size():end+1;
  }
  return true;
}
} // namespace lunaredu::desktop
