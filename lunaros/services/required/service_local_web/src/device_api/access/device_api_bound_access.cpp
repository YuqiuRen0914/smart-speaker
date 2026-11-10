#include "device_api/access/device_api_bound_access.h"
#include "device_api/access/device_api_maintenance_access.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <string>
#include <string_view>

#include "lunaros_hmac.h"

#include "lunaros_platform_link.h"
#include "lunaredu_device_profiles.h"
#include "system/identity.h"
#include "configuration/device_config.h"
#include "configuration/lifecycle.h"
#include "diagnostics/session.h"

namespace {

bool decimal(std::string_view value) {
  return lunaredu::protocol::profiles::auth_decimal(value);
}

std::string_view board_authority(const char *url) {
  if (url == nullptr) return {};
  std::string_view value(url);
  const size_t scheme = value.find("://");
  if (scheme == std::string_view::npos) return {};
  value.remove_prefix(scheme + 3);
  if (value.empty() || value.find_first_of("/?#@|") != std::string_view::npos)
    return {};
  return value;
}

bool constant_time_hex_equal(std::string_view actual,
                             const std::array<unsigned char, 32> &expected) {
  std::array<uint8_t,32> decoded{};
  return lunaredu::lunaros::digest::decode_hex(actual,decoded.data(),decoded.size()) &&
      lunaredu::lunaros::digest::constant_time_equal(decoded.data(),expected.data(),expected.size());
}

} // namespace

bool device_api_bound_access_required() {
  // Local network/server repair and OTA must remain usable when the business
  // platform is offline, misconfigured or has not restored binding identity.
  return false;
}

const char *device_api_admin_readiness_error() {
  return device_api_maintenance_readiness_error();
}

namespace {
bool credential_hmac(const std::string &message, std::array<unsigned char, 32> &digest) {
  std::string credential = lunaredu::lunaros::device_cloud_credential();
  if (credential.empty()) return false;
  std::array<unsigned char, 32> key{};
  // Board grants derive their MAC key from SHA256(credential); provisioning
  // uses its proof directly. Keep this protocol policy outside the primitive.
  const bool derived = lunaredu::lunaros::digest::sha256(
      reinterpret_cast<const uint8_t *>(credential.data()), credential.size(), key);
  lunaredu::lunaros::digest::clear_secret(reinterpret_cast<uint8_t *>(credential.data()),credential.size());
  const bool ok = derived && lunaredu::lunaros::digest::hmac_sha256(key.data(), key.size(),
          reinterpret_cast<const uint8_t *>(message.data()), message.size(), digest);
  lunaredu::lunaros::digest::clear_secret(key.data(), key.size());
  return ok;
}


} // namespace

static bool verify_scoped_grant(const char *grant, const char *purpose) {
  if (grant == nullptr ||
      !lunaredu::lunaros::platform_link_local_authority_ready()) return false;
  const std::string_view token(grant);
  if (token.size() > 256) return false;
  std::array<std::string_view, 7> parts{};
  size_t cursor = 0;
  for (size_t index = 0; index < parts.size(); ++index) {
    const size_t end = token.find('|', cursor);
    if ((index + 1 < parts.size()) == (end == std::string_view::npos)) return false;
    parts[index] = token.substr(cursor, end == std::string_view::npos ? end : end - cursor);
    cursor = end + 1;
  }
  const auto binding = lunaredu::lunaros::platform_link_pairing_snapshot();
  const auto origin = lunaredu::lunaros::platform_link_binding_browser_origin();
  if (parts[0] != purpose || !decimal(parts[1]) || !decimal(parts[2]) || !lunaredu::protocol::profiles::auth_hex(parts[3],32) || !decimal(parts[4]) ||
      parts[5] != board_authority(origin.c_str()) || binding.access_epoch < 0 ||
      binding.revocation_requested || binding.persistence_pending) return false;
  const bool shared = std::string_view(purpose) == lunaredu::protocol::profiles::auth_shared;
  std::string manager = binding.control_manager_id;
  manager.erase(std::remove(manager.begin(), manager.end(), '-'), manager.end());
  if ((shared && !binding.shared_control_enabled) || (!manager.empty() && parts[3] != manager)) return false;
  const auto epoch = std::strtoull(std::string(parts[1]).c_str(), nullptr, 10);
  const auto access_epoch = std::strtoull(std::string(parts[2]).c_str(), nullptr, 10);
  const auto expires = std::strtoll(std::string(parts[4]).c_str(), nullptr, 10);
  const auto now = static_cast<long long>(std::time(nullptr));
  if (epoch != static_cast<unsigned long long>(binding.binding_epoch) || access_epoch != static_cast<unsigned long long>(binding.access_epoch) ||
      now < 1700000000LL || expires <= now || expires > now + (shared ? lunaredu::protocol::profiles::auth_shared_ttl + lunaredu::protocol::profiles::auth_shared_clock_skew : lunaredu::protocol::profiles::auth_owner_ttl + lunaredu::protocol::profiles::auth_owner_clock_skew)) return false;
  const auto message=shared ? lunaredu::protocol::profiles::auth_message_shared(
      lunaredu::lunaros::hardware_serial_number(),parts[1],parts[2],parts[3],parts[4],parts[5]) :
      lunaredu::protocol::profiles::auth_message_owner(
      lunaredu::lunaros::hardware_serial_number(),parts[1],parts[2],parts[3],parts[4],parts[5]);
  if(message.empty())return false;
  std::array<unsigned char, 32> digest{};
  return credential_hmac(message, digest) && constant_time_hex_equal(parts[6], digest);
}

bool device_api_shared_access_authorized(const char *grant) {
  return verify_scoped_grant(grant, lunaredu::protocol::profiles::auth_shared);
}
bool device_api_bound_access_authorized(const char *grant) {
  if (!device_api_bound_access_required()) return true;
  return verify_scoped_grant(grant, lunaredu::protocol::profiles::auth_owner) ||
      device_api_maintenance_session_authorized(grant);
}

bool device_api_shared_control_id_authorized(const char *grant, const char *control_id) {
  if (!device_api_shared_access_authorized(grant) || control_id == nullptr) return false;
  const std::string token(grant);
  size_t cursor = 0;
  for (int index = 0; index < 3; ++index) cursor = token.find('|', cursor) + 1;
  const std::string prefix = token.substr(cursor, 32) + "_";
  return std::string_view(control_id).substr(0,prefix.size()) == prefix;
}

bool device_api_shared_operation_allowed(const char *module, const char *operation) {
  const std::string_view m = module == nullptr ? "" : module;
  const std::string_view op = operation == nullptr ? "" : operation;
  return (m == "desktop_pet" && (op == "state.get" || op == "teleop.begin" || op == "teleop.setpoint" ||
    op == "teleop.stop" || op == "teleop.state.get")) ||
    ((m == "controller" || m == "remote_controller") && (op == "state.get" || op == "status.get" || op == "inputs.get"));
}

bool device_api_admin_access_authorized(const char *grant) {
  // Unbound onboarding is public, but must never imply administrator authority.
  return device_api_maintenance_session_authorized(grant);
}


bool device_api_diagnostic_access_authorized(
    const lunaredu::lunaros::DeviceOperationSpec *operation, const char *token) {
  if (operation == nullptr) return false;
  return device_api_admin_access_authorized(token) ||
      lunaredu::lunaros::diagnostic_session_authorizes(operation, token);
}
