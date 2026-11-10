#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace lunaredu::lunaros {
// Startup metadata only, independent of online/session readiness. Products
// choose exposure policy; providers own the names. First occurrence wins.
void append_platform_link_capability(std::vector<std::string> &,const char *);
std::vector<std::string> normalize_platform_link_capabilities(const std::vector<std::string> &);
template<std::size_t Count>
std::vector<std::string> platform_link_capabilities(const std::array<const char *,Count> &providers){
  std::vector<std::string> result;result.reserve(Count);
  for(const auto *name:providers)append_platform_link_capability(result,name);
  return result;
}
}
