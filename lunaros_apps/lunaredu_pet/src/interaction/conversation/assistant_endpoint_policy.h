#pragma once

#include <cstdint>
#include <string>

namespace lunaredu::pet::conversation::internal {

inline bool valid_activation_endpoint(const std::string &url) {
  if (url.empty()) return true; // Empty selects the pinned official default.
  constexpr char prefix[] = "https://";
  constexpr size_t prefix_size = sizeof(prefix) - 1;
  if (url.size() <= prefix_size || url.size() > 255 ||
      url.compare(0, prefix_size, prefix) != 0) return false;
  const size_t path_start = url.find('/', prefix_size);
  const std::string authority = url.substr(prefix_size, path_start - prefix_size);
  if (authority.empty() || authority.find('@') != std::string::npos ||
      authority.find('?') != std::string::npos || authority.find('#') != std::string::npos)
    return false;
  std::string host = authority;
  const size_t colon = authority.rfind(':');
  if (colon != std::string::npos) {
    host = authority.substr(0, colon);
    const std::string port = authority.substr(colon + 1);
    if (port.empty() || port.size() > 5) return false;
    uint32_t value = 0;
    for (const char c : port) {
      if (c < '0' || c > '9') return false;
      value = value * 10U + static_cast<uint32_t>(c - '0');
    }
    if (value == 0 || value > 65535) return false;
  }
  if (host.empty() || host.front() == '.' || host.back() == '.' ||
      host.front() == '-' || host.back() == '-') return false;
  bool previous_dot = false;
  for (const char c : host) {
    const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                         (c >= '0' && c <= '9') || c == '.' || c == '-';
    if (!allowed || (c == '.' && previous_dot)) return false;
    previous_dot = c == '.';
  }
  if (path_start != std::string::npos) {
    bool previous_slash = false;
    for (size_t i = path_start; i < url.size(); ++i) {
      const char c = url[i];
      const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '/' || c == '-' ||
                           c == '_' || c == '.' || c == '~';
      if (!allowed || (c == '/' && previous_slash)) return false;
      previous_slash = c == '/';
    }
    if (url.find("..", path_start) != std::string::npos) return false;
    std::string path = url.substr(path_start);
    while (!path.empty() && path.back() == '/') path.pop_back();
    if (path.size() >= 9 && path.compare(path.size() - 9, 9, "/activate") == 0)
      return false;
  }
  return true;
}

using ActivationEndpointPersist = bool (*)(const char *, const std::string &);

// A server change must never retain credentials issued by another server.
// Persistence precedes publication so failure leaves the active endpoint intact.
inline bool commit_activation_endpoint(const std::string &candidate,
                                      bool credentials_present,
                                      bool activation_pending,
                                      std::string &current,
                                      ActivationEndpointPersist persist) {
  if (!valid_activation_endpoint(candidate) || credentials_present ||
      activation_pending || persist == nullptr) return false;
  if (candidate == current) return true;
  if (!persist("ll_url", candidate)) return false;
  current = candidate;
  return true;
}

} // namespace lunaredu::pet::conversation::internal
