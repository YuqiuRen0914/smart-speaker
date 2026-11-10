#pragma once

#include <string_view>

namespace lunaredu::lunaros {

// Team entry names are <application>.<workstation>.local/net/share.lunaredu.cn.
// The caller retains its application and operation authorization boundary.
inline bool network_is_development_application_host(std::string_view host,
                                                     std::string_view application) {
  if (host.substr(0, application.size()) != application || host.size() <= application.size() ||
      host[application.size()] != '.') return false;
  host.remove_prefix(application.size() + 1);
  constexpr std::string_view suffix = ".lunaredu.cn";
  if (host.size() < suffix.size() || host.substr(host.size() - suffix.size()) != suffix) return false;
  host.remove_suffix(suffix.size());
  const auto dot = host.find('.');
  if (dot == std::string_view::npos) return false;
  const auto alias = host.substr(0, dot), realm = host.substr(dot + 1);
  if (alias.size() < 2 || alias.size() > 32 || alias.front() < 'a' ||
      alias.front() > 'z' || alias == "net" || alias == "share") return false;
  const auto alphanumeric = [](char value) {
    return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9');
  };
  if (!alphanumeric(alias.back())) return false;
  for (const char character : alias)
    if (!alphanumeric(character) && character != '-') return false;
  return realm == "local" || realm == "net" || realm == "share";
}

} // namespace lunaredu::lunaros
