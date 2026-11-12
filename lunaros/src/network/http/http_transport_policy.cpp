#include "network/http/network_transport_policy.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace lunaredu::lunaros {

namespace {

bool parse_ipv4(std::string_view value, std::array<unsigned int, 4> &parts) {
  size_t start = 0;
  for (size_t index = 0; index < parts.size(); ++index) {
    const size_t end = value.find('.', start);
    if ((index < parts.size() - 1 && end == std::string_view::npos) ||
        (index == parts.size() - 1 && end != std::string_view::npos)) {
      return false;
    }
    const size_t limit = end == std::string_view::npos ? value.size() : end;
    if (limit <= start || limit - start > 3) return false;
    unsigned int part = 0;
    for (size_t cursor = start; cursor < limit; ++cursor) {
      const unsigned char byte = static_cast<unsigned char>(value[cursor]);
      if (!std::isdigit(byte)) return false;
      part = part * 10U + static_cast<unsigned int>(byte - '0');
    }
    if (part > 255U) return false;
    parts[index] = part;
    start = limit + 1;
  }
  return true;
}

bool is_private_ipv4(std::string_view value) {
  std::array<unsigned int, 4> parts{};
  if (!parse_ipv4(value, parts)) return false;
  return parts[0] == 10U || parts[0] == 127U ||
         (parts[0] == 192U && parts[1] == 168U) ||
         (parts[0] == 172U && parts[1] >= 16U && parts[1] <= 31U);
}

bool ends_with(std::string_view value, std::string_view suffix) {
  return value.size() >= suffix.size() &&
         value.substr(value.size() - suffix.size()) == suffix;
}

std::string plaintext_hostname(const std::string &url, bool local_port) {
  constexpr std::string_view kScheme = "http://";
  if (url.rfind(kScheme, 0) != 0) return {};
  const size_t authority_start = kScheme.size();
  const size_t authority_end = url.find_first_of("/?#", authority_start);
  std::string authority = url.substr(
      authority_start, authority_end == std::string::npos
                           ? std::string::npos
                           : authority_end - authority_start);
  if (authority.empty() || authority.find('@') != std::string::npos ||
      authority.front() == '[') {
    return {};
  }
  const size_t port = authority.find(':');
  if (port != std::string::npos) {
    const auto port_text = authority.substr(port + 1);
    if (!local_port && port_text != "80") return {};
    if (port_text.empty() || port_text.size() > 5) return {};
    unsigned int value = 0;
    for (unsigned char c : port_text) {
      if (c < '0' || c > '9') return {};
      value = value * 10 + (c - '0');
    }
    if (value == 0 || value > 65535) return {};
    authority.resize(port);
  }
  std::transform(authority.begin(), authority.end(), authority.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  return authority;
}

bool is_plaintext_development_url(const std::string &url, bool local_port) {
  const std::string host = plaintext_hostname(url, local_port);
  if (host.empty()) return false;
  if (host == "localhost" || is_private_ipv4(host) ||
      ends_with(host, ".local")) {
    return true;
  }
  if (local_port) return false;

  // The local developer gateway uses
  // tools.lunaredu.<private-ip>.nip.io. Only accept nip.io when the embedded
  // address is private, so opting into development plaintext cannot silently
  // weaken arbitrary Internet requests.
  constexpr std::string_view kNipSuffix = ".nip.io";
  if (!ends_with(host, kNipSuffix)) return false;
  const std::string_view prefix(host.data(), host.size() - kNipSuffix.size());
  const size_t first_octet = prefix.rfind('.', prefix.rfind('.') - 1);
  if (first_octet == std::string_view::npos) return false;
  const size_t second_octet = prefix.rfind('.', first_octet - 1);
  if (second_octet == std::string_view::npos) return false;
  const size_t third_octet = prefix.rfind('.', second_octet - 1);
  if (third_octet == std::string_view::npos) return false;
  return is_private_ipv4(prefix.substr(third_octet + 1));
}

} // namespace

bool network_transport_security_allows(
    const std::string &url, NetworkTransportSecurity requested_security,
    bool require_tls_for_production) {
  if (url.rfind("https://", 0) == 0) {
    return true;
  }
  if (url.rfind("http://", 0) != 0) {
    return false;
  }
  if (!require_tls_for_production) return true;
  if (requested_security == NetworkTransportSecurity::AllowPlaintextLocal)
    return is_plaintext_development_url(url, true);
  return requested_security ==
             NetworkTransportSecurity::AllowPlaintextDevelopment &&
         is_plaintext_development_url(url, false);
}

bool network_transport_policy_failure(const std::string &detail) {
  return detail == kNetworkTransportPolicyError;
}

} // namespace lunaredu::lunaros
