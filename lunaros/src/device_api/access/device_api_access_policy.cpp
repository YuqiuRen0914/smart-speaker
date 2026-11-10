#include "device_api/access/device_api_access_policy.h"

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
         std::strcmp(operation, "network.provision.status") == 0 ||
         std::strcmp(operation, "network.provision.scan") == 0 ||
         std::strcmp(operation, "network.provision.submit") == 0;
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
      host == "tools.lunaredu.localhost" || host == "board.lunaredu.localhost") {
    return true;
  }

  // Tools remains accepted while existing installations receive the Board migration firmware.
  const std::string_view prefix = host.substr(0, 15) == "board.lunaredu."
      ? std::string_view("board.lunaredu.") : std::string_view("tools.lunaredu.");
  constexpr std::string_view suffix = ".nip.io";
  if (host.size() < prefix.size() + suffix.size() ||
      host.substr(0, prefix.size()) != prefix ||
      host.substr(host.size() - suffix.size()) != suffix ||
      host.size() <= prefix.size() + suffix.size()) {
    return false;
  }
  return valid_ipv4(host.substr(prefix.size(),
                                host.size() - prefix.size() - suffix.size()));
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
  if (!provisioning_softap_active) {
    return true;
  }
  return surface == DeviceApiAccessSurface::Operation &&
         is_provisioning_operation(operation);
}

bool device_api_origin_allowed(const char *origin, const char *device_local_ip) {
  if (origin == nullptr || origin[0] == '\0') {
    return true;
  }
  const std::string_view value(origin);
  if (device_local_ip != nullptr && valid_ipv4(device_local_ip) &&
      std::strcmp(device_local_ip, "0.0.0.0") != 0) {
    const std::string own_origin = std::string("http://") + device_local_ip;
    if (value == own_origin || value == own_origin + ":80") return true;
  }
  if (value == "https://board.lunaredu.cn" || value == "https://tools.lunaredu.cn") {
    return true;
  }
  constexpr std::string_view development_scheme = "http://";
  if (value.size() < development_scheme.size() ||
      value.substr(0, development_scheme.size()) != development_scheme) {
    return false;
  }
  const std::string_view authority = value.substr(development_scheme.size());
  if (authority.empty() || authority.find_first_of("/?#@") !=
                               std::string_view::npos) {
    return false;
  }
  return local_development_host(authority) ||
         provisioning_device_host(authority);
}
