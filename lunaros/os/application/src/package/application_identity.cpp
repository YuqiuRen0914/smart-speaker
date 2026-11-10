#include "app/package/application_identity.h"

namespace lunaredu::lunaros::apps {
bool id_valid(const std::string &v) {
  if (v.size() < 3 || v.size() > 47 || v.front() < 'a' || v.front() > 'z')
    return false;
  bool dot = false;
  for (char c : v) {
    if (c == '.')
      dot = true;
    else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
      return false;
  }
  return dot && v.back() != '.' && v.find("..") == std::string::npos;
}

bool version_valid(const std::string &v) {
  if (v.empty() || v.size() > 11)
    return false;
  int dots = 0, digits = 0;
  for (char c : v) {
    if (c == '.') {
      if (!digits || digits > 3)
        return false;
      digits = 0;
      dots++;
    } else if (c >= '0' && c <= '9')
      digits++;
    else
      return false;
  }
  return dots == 2 && digits > 0 && digits <= 3;
}

bool digest_valid(const std::string &v) {
  if (v.size() != 64)
    return false;
  for (char c : v)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
      return false;
  return true;
}

bool version_parts(const std::string &v, unsigned (&parts)[3]) {
  if (!version_valid(v)) return false;
  size_t at=0;
  for(int i=0;i<3;++i){parts[i]=0;while(at<v.size()&&v[at]!='.')parts[i]=parts[i]*10+unsigned(v[at++]-'0');if(i<2)++at;}
  return true;
}
}
