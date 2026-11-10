#include "device_api/access/device_api_access_policy.h"
#include "lunaros_network_development_domain.h"

#include <cctype>
#include <cstring>
#include <string_view>
#include <string>

namespace {

bool is_provisioning_operation(const char *operation) {
  if (operation == nullptr) {
    return false;
  }
  return std::strcmp(operation, "identity.status.get") == 0 ||
         std::strcmp(operation, "identity.name.set") == 0 ||
         std::strcmp(operation, "system.info.get") == 0 ||
         std::strcmp(operation, "provisioning.status") == 0 ||
         std::strcmp(operation, "provisioning.scan") == 0 ||
         std::strcmp(operation, "provisioning.submit") == 0 ||
         std::strcmp(operation, "provisioning.stop") == 0 ||
         std::strcmp(operation, "provisioning.cancel") == 0 ||
         std::strcmp(operation, "bionic_hand.state.get") == 0 ||
         std::strcmp(operation, "bionic_hand.gesture.set") == 0 ||
         std::strcmp(operation, "bionic_hand.finger.set") == 0 ||
         std::strcmp(operation, "bionic_hand.motion.stop") == 0 ||
         std::strcmp(operation, "bionic_hand.output.off") == 0;
}

bool valid_port(std::string_view value) {
  if (value.empty() || value.size() > 5) {
    return false;
  }
  uint32_t port = 0;
  for (const char character : value) {
    if (!std::isdigit(static_cast<unsigned char>(character))) {
      return false;
    }
    port = port * 10 + static_cast<uint32_t>(character - '0');
  }
  return port > 0 && port <= 65535;
}

bool valid_ipv4(std::string_view value) {
  size_t cursor = 0;
  for (uint8_t part = 0; part < 4; ++part) {
    const size_t end = value.find('.', cursor);
    const size_t part_end = end == std::string_view::npos ? value.size() : end;
    if (part_end == cursor || part_end - cursor > 3) {
      return false;
    }
    uint16_t octet = 0;
    for (size_t index = cursor; index < part_end; ++index) {
      const char character = value[index];
      if (!std::isdigit(static_cast<unsigned char>(character))) {
        return false;
      }
      octet = static_cast<uint16_t>(octet * 10 + character - '0');
    }
    if (octet > 255 || (part < 3 && end == std::string_view::npos) ||
        (part == 3 && end != std::string_view::npos)) {
      return false;
    }
    cursor = part_end + 1;
  }
  return true;
}

bool local_board_development_host(std::string_view authority) {
  std::string_view host = authority;
  const size_t port = authority.rfind(':');
  if (port != std::string_view::npos) {
    if (!valid_port(authority.substr(port + 1))) return false;
    host = authority.substr(0, port);
  }
  if (host == "board.lunaredu.localhost") return true;
  return lunaredu::lunaros::network_is_development_application_host(host, "board");
}

bool local_development_host(std::string_view authority) {
  std::string_view host = authority;
  if (!authority.empty() && authority.front() == '[') {
    const size_t closing = authority.find(']');
    if (closing == std::string_view::npos) {
      return false;
    }
    host = authority.substr(0, closing + 1);
    const std::string_view remainder = authority.substr(closing + 1);
    if (!remainder.empty() &&
        (remainder.front() != ':' || !valid_port(remainder.substr(1)))) {
      return false;
    }
  } else {
    const size_t separator = authority.rfind(':');
    if (separator != std::string_view::npos) {
      if (!valid_port(authority.substr(separator + 1))) {
        return false;
      }
      host = authority.substr(0, separator);
    }
  }

  if (host == "localhost" || host == "127.0.0.1" || host == "[::1]" ||
      host == "board.lunaredu.localhost") {
    return true;
  }

  return lunaredu::lunaros::network_is_development_application_host(host, "board");
}

bool provisioning_device_host(std::string_view authority) {
  constexpr std::string_view host = "192.168.4.1";
  if (authority == host) {
    return true;
  }
  constexpr std::string_view explicit_http_port = "192.168.4.1:80";
  return authority == explicit_http_port;
}

} // namespace

bool device_api_access_policy_allows(bool provisioning_softap_active,
                                    DeviceApiAccessSurface surface,
                                    const char *operation) {
  // Network, server selection and OTA are local maintenance, including when
  // the only reachable page is the provisioning hotspot.
  if (surface == DeviceApiAccessSurface::Operation && operation) {
    const std::string_view name(operation);
    if (name.starts_with("network.") || name.starts_with("ota.") ||
        name.starts_with("assistant_endpoint.") ||
        name == "platform_link.status.get" ||
        name.starts_with("platform_link.environment.") ||
        name == "system.restart") return true;
  }
  if (!provisioning_softap_active) {
    return true;
  }
  return surface == DeviceApiAccessSurface::Operation &&
         is_provisioning_operation(operation);
}

bool device_api_origin_allowed(const char *origin, const char *device_local_ip,
                               const char *board_base_url) {
  if (origin == nullptr || origin[0] == '\0') {
    return true;
  }
  const std::string_view value(origin);
  if (value == "https://board.lunaredu.cn") return true;
  if (device_local_ip != nullptr && valid_ipv4(device_local_ip) &&
      std::strcmp(device_local_ip, "0.0.0.0") != 0) {
    const std::string own_origin = std::string("http://") + device_local_ip;
    if (value == own_origin || value == own_origin + ":80") return true;
  }
  constexpr std::string_view http_scheme = "http://";
  constexpr std::string_view https_scheme = "https://";
  const bool is_http = value.substr(0, http_scheme.size()) == http_scheme;
  const bool is_https = value.substr(0, https_scheme.size()) == https_scheme;
  if (!is_http && !is_https) {
    return false;
  }
  const std::string_view authority = value.substr(is_https ? https_scheme.size() : http_scheme.size());
  if (authority.empty() || authority.find_first_of("/?#@") !=
                               std::string_view::npos) {
    return false;
  }
  if (is_http && provisioning_device_host(authority)) return true;
  // The Board development gateway may move between private LAN addresses.
  // Keep its browser origin independent from the device's cloud environment.
  if (is_https && local_board_development_host(authority)) return true;
  if (board_base_url == nullptr) return false;
  const std::string_view configured(board_base_url);
  const bool configured_http = configured.substr(0, http_scheme.size()) == http_scheme;
  const bool configured_https = configured.substr(0, https_scheme.size()) == https_scheme;
  if (!configured_http && !configured_https) return false;
  const std::string_view configured_authority = configured.substr(
      configured_https ? https_scheme.size() : http_scheme.size());
  if (configured_authority.empty() ||
      configured_authority.find_first_of("/?#@") != std::string_view::npos ||
      authority != configured_authority) return false;
  // Production never falls back to cleartext. The local Board gateway may
  // serve HTTPS to the browser while PlatformLink itself uses plain HTTP.
  if (configured_https) return is_https;
  return local_development_host(configured_authority);
}
