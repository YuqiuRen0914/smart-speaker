#include "lunaros_platform_link_capabilities.h"
#include <algorithm>
#include <string_view>
namespace lunaredu::lunaros {
void append_platform_link_capability(std::vector<std::string> &values,const char *name){
  // shared_access_v1 is published by this protocol owner, once, on every path.
  if(!name||!*name||std::string_view(name)=="shared_access_v1")return;
  if(std::find(values.begin(),values.end(),name)==values.end())values.emplace_back(name);
}
std::vector<std::string> normalize_platform_link_capabilities(const std::vector<std::string> &values){
  std::vector<std::string> result;result.reserve(values.size());
  for(const auto &value:values)append_platform_link_capability(result,value.c_str());
  return result;
}
}
