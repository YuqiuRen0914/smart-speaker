#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include "lunaros_network_transport_policy.h"
#include "lunaros_network_development_domain.h"
#include "configuration/device_config.h"

namespace lunaredu::lunaros::platform_link {
inline constexpr char kDefaultProductionDeviceOrigin[] = "https://gateway.lunaredu.cn";
inline constexpr char kDefaultDevelopmentDeviceOrigin[] = "https://gateway.lunaredu.localhost";

// Accept an HTTPS origin, or the Board LunarLink page copied from a browser.
// Development and production use the same transport. Preserve HTTPS exactly.
inline bool normalize_development_origin(std::string input, std::string &origin,
    const std::string &preferred_device_origin=kDefaultProductionDeviceOrigin) {
  const auto first = input.find_first_not_of(" \r\n\t");
  if (first == std::string::npos) return false;
  input = input.substr(first, input.find_last_not_of(" \r\n\t") - first + 1);
  if (input.size() > kDeviceCloudBaseUrlMax ||
      input.find_first_of("@?#\\") != std::string::npos ||
      std::any_of(input.begin(), input.end(), [](unsigned char c) { return c <= 32 || c >= 127; })) return false;
  const size_t scheme = input.find("://");
  if (scheme == std::string::npos) return false;
  const auto path = input.find('/', scheme + 3);
  if (path != std::string::npos) {
    if (input.substr(path) != "/" && input.substr(path) != "/link" && input.substr(path) != "/link/") return false;
    input.resize(path);
  }
  std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) { return std::tolower(c); });
  std::string host = input.substr(scheme + 3);
  const auto hostname_before_port = host.substr(0, host.find(':'));
  const bool official_localhost = hostname_before_port == "device.lunaredu.localhost" ||
      hostname_before_port == "gateway.lunaredu.localhost" || hostname_before_port == "board.lunaredu.localhost";
  if (host.empty() || host == "localhost" || (host.find("localhost") != std::string::npos && !official_localhost) ||
      host.rfind("127.", 0) == 0 || host == "0.0.0.0" ||
      host.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-:") != std::string::npos) return false;
  const size_t port = host.find(':');
  if (port != std::string::npos) {
    const auto number = host.substr(port + 1);
    if (number.empty() || number.size() > 5 ||
        number.find_first_not_of("0123456789") != std::string::npos) return false;
    const int value = std::stoi(number);
    if (value < 1 || value > 65535) return false;
    if ((input.rfind("https://", 0) == 0 && number == "443") ||
        (input.rfind("http://", 0) == 0 && number == "80")) {
      host.resize(port);
      input = input.substr(0, scheme + 3) + host;
    }
  }
  const auto hostname = host.substr(0, host.find(':'));
  if (hostname.empty() || hostname.size() > 253 || hostname.back() == '.') return false;
  for (size_t start = 0; start < hostname.size();) {
    const size_t dot = hostname.find('.', start);
    const size_t end = dot == std::string::npos ? hostname.size() : dot;
    if (end == start || end - start > 63 || hostname[start] == '-' || hostname[end - 1] == '-') return false;
    start = end + 1;
  }
  if (!network_transport_security_allows(input,
          NetworkTransportSecurity::RequireTls)) return false;
  (void)preferred_device_origin;
  // Official Board links and retired Device origins use the current Gateway.
  if (hostname == "board.lunaredu.cn" || hostname == "board.lunaredu.localhost" || network_is_development_application_host(hostname, "board"))
    input.replace(scheme+3,6,"gateway.");
  else if (hostname == "device.lunaredu.cn" || hostname == "device.lunaredu.localhost" || network_is_development_application_host(hostname, "device"))
    input.replace(scheme+3,7,"gateway.");
  origin = input;
  return true;
}

inline std::string binding_browser_origin(const std::string &origin) {
  // Gateway handles device requests; Board hosts the human claim page.
  if (origin.rfind("https://",0)!=0) return {};
  const auto host = origin.substr(8,origin.find(':',8)-8);
  for (const auto *prefix : {"gateway.","device."}) {
    const std::string name(prefix);
    if (host == name+"lunaredu.cn" || host == name+"lunaredu.localhost" ||
        network_is_development_application_host(host, name.substr(0, name.size() - 1)))
      return "https://board."+origin.substr(8+name.size());
  }
  if (origin.rfind("https://gateway.", 0) == 0 || origin.rfind("https://device.",0)==0) return {};
  if (origin.rfind("https://", 0) == 0) return origin;
  return {};
}
// TLS is required in both environments. A custom HTTPS endpoint is not the
// production deployment merely because it uses the same secure transport.
inline bool remote_origin_is_production(const std::string &remote,
                                        const std::string &production) {
  std::string selected, official;
  const std::string preferred=production.empty()?kDefaultProductionDeviceOrigin:production;
  if(!normalize_development_origin(remote,selected,preferred)||
      !normalize_development_origin(preferred,official,preferred))return false;
  if(production.empty() && (selected=="https://device.lunaredu.cn" ||
      selected=="https://gateway.lunaredu.cn"))return true;
  return selected==official;
}
} // namespace lunaredu::lunaros::platform_link
