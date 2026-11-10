#include "platform_link_state.h"
#include "platform_link_cloud_ota.h"

#include <algorithm>
#include <utility>
#include <mutex>

#include "configuration/settings.h"
#include "configuration/device_config.h"
#include "configuration/lifecycle.h"
#include "platform_link_http.h"

namespace lunaredu::lunaros::platform_link {
namespace {

constexpr const char *kNamespace = "lunar_link";
constexpr const char *kKey = "binding";
std::mutex g_cache_mutex;
std::string g_persisted;
struct BindingSafety { bool present=false,ever_bound=false,never_bound=false; };
bool read_safety(BindingSafety &safety,std::string &error) {
  std::string text;
  const auto read=settings_read_string(kNamespace,"safety",256,text,error);
  if(read==SettingsReadResult::Failed)return false;
  if(read==SettingsReadResult::Missing){error.clear();return true;}
  JsonDocument doc;
  if(deserializeJson(doc,text)||!doc["schema"].is<int>()||doc["schema"].as<int>()!=1||
      !doc["everBound"].is<bool>()||!doc["neverBound"].is<bool>()||
      doc["everBound"].as<bool>()==doc["neverBound"].as<bool>()) {
    error="invalid_binding_safety";return false;
  }
  safety={true,doc["everBound"].as<bool>(),doc["neverBound"].as<bool>()};return true;
}
bool write_safety(bool ever_bound,std::string &error) {
  if(!settings_set_string(kNamespace,"safety",ever_bound ?
      R"({"schema":1,"everBound":true,"neverBound":false})" :
      R"({"schema":1,"everBound":false,"neverBound":true})")) {
    error="binding_safety_commit_failed";return false;
  }
  return true;
}
bool has_bound_evidence(JsonVariantConst value) {
  return (value["accountId"].is<const char *>() &&
          !value["accountId"].as<std::string>().empty()) ||
      (value["revocationRequested"] | false) || (value["state"] | std::string{})=="bound";
}

void read_avatar_resource(JsonVariantConst data,
                          PlatformLinkPairingSnapshot &snapshot) {
  const std::string revision = data["avatarResourceRevision"] | "";
  const std::string url = data["avatarResourceUrl"] | "";
  const std::string format = data["avatarResourceFormat"] | "";
  const uint16_t width = data["avatarResourceWidth"] | 0;
  const uint16_t height = data["avatarResourceHeight"] | 0;
  const size_t maximum_bytes = data["avatarMaximumBytes"] | 0;
  if (!(data["avatarResourceAvailable"] | false) || revision.empty() ||
      url.empty() || format.empty() || width == 0 || height == 0 ||
      maximum_bytes == 0) return;
  snapshot.account_avatar_resource_available = true;
  snapshot.account_avatar_resource_revision = revision;
  snapshot.account_avatar_resource_url = url;
  snapshot.account_avatar_resource_format = format;
  snapshot.account_avatar_resource_width = width;
  snapshot.account_avatar_resource_height = height;
  snapshot.account_avatar_resource_maximum_bytes = maximum_bytes;
}

std::string profile_revision() {
  return std::to_string(device_cloud_credential_fingerprint());
}

} // namespace

std::string binding_profile_tag() {
  const auto origin=platform_base_url();
  return std::to_string(origin.size())+":"+origin+":"+profile_revision();
}

bool read_cached_binding(PlatformLinkPairingSnapshot &cached, std::string &error,
                         bool *authority_restored,bool *local_unbound) {
  if (authority_restored) *authority_restored=false;
  if (local_unbound) *local_unbound=false;
  std::lock_guard<std::mutex> lock(g_cache_mutex);
  g_persisted.clear();
  cached = {};
  BindingSafety safety;
  if(!read_safety(safety,error))return false;
  std::string serialized;
  const auto read = settings_read_string(kNamespace, kKey, 4096, serialized, error);
  if (read == SettingsReadResult::Failed) return false;
  if (read == SettingsReadResult::Missing) {
    // lunar_link/binding is the durable binding owner. Historical avatar
    // fixtures are not additional binding stores. Never probe invented NVS
    // namespaces (the SDK namespace limit is 15 characters).
    if(!safety.ever_bound&&!configuration_recovery_active()) {
      if(!safety.present&&!write_safety(false,error))return false;
      if(local_unbound)*local_unbound=true;
    }
    error.clear();return true;
  }
  JsonDocument document;
  if (deserializeJson(document, serialized) || !document.is<JsonObject>()) {
    error = "invalid_binding_record";
    return false;
  }
  if(has_bound_evidence(document.as<JsonVariantConst>())&&!safety.ever_bound) {
    if(!write_safety(true,error))return false;
    safety={true,true,false};
  }
  // A binding record belongs to one server and credential generation.
  const std::string origin = document["platformOrigin"] | "";
  const std::string revision = document["profileRevision"] | "";
  if (origin != platform_base_url() || revision != profile_revision()) {
    error.clear(); return true;
  }
  if (!document["bindingEpoch"].is<int>() || !document["accountId"].is<const char *>()) {
    error = "invalid_binding_record"; return false;
  }
  if(!document["revocationRequested"].isNull()&&!document["revocationRequested"].is<bool>()) {
    error="invalid_binding_record";return false;
  }
  if(!document["bindingClaimRequired"].isNull()&&!document["bindingClaimRequired"].is<bool>()) {
    error="invalid_binding_record";return false;
  }
  cached.binding_epoch = document["bindingEpoch"].as<int>();
  const auto account = document["accountId"].as<JsonString>();
  cached.account_id.assign(account.c_str(), account.size());
  if (cached.binding_epoch < 0 || cached.account_id.size() > 256 ||
      cached.account_id.find('\0') != std::string::npos) {
    error = "invalid_binding_record"; return false;
  }
  if(document["state"].is<const char *>() && cached.account_id.empty() &&
      document["state"].as<std::string>()!="unbound") {
    error="invalid_binding_record";return false;
  }
  // An old explicit record is not evidence of never having been bound.
  // Its loss must not later be treated as a new-device migration.
  if(!safety.present&&!write_safety(true,error))return false;
  cached.state = cached.account_id.empty() ? PlatformLinkBindingState::Unbound
                                         : PlatformLinkBindingState::Bound;
  cached.revocation_requested = document["revocationRequested"] | false;
  cached.binding_claim_required = document["bindingClaimRequired"] | cached.revocation_requested;
  if (cached.binding_claim_required && !cached.revocation_requested && !cached.account_id.empty()) {
    error="invalid_binding_record";return false;
  }
  if (cached.revocation_requested) {
    const int epoch = cached.binding_epoch;
    cached = {};
    cached.binding_epoch = epoch;
    cached.revocation_requested = true;
    cached.binding_claim_required = true;
  }
  if (!cached.account_id.empty()) {
    cached.account_display_name = document["accountDisplayName"] | "";
    cached.account_avatar_url = document["accountAvatarUrl"] | "";
    cached.account_can_manage_hardware = document["accountCanManageHardware"] | false;
    read_avatar_resource(document.as<JsonVariantConst>(), cached);
  }
  g_persisted = std::move(serialized);
  if (authority_restored) *authority_restored=true;
  if(local_unbound)*local_unbound=cached.account_id.empty();
  error.clear();
  return true;
}

bool persist_cached_binding(const PlatformLinkPairingSnapshot &current, std::string &error) {
  std::lock_guard<std::mutex> lock(g_cache_mutex);
  error.clear();
  if (current.binding_epoch < 0 || current.account_id.size() > 256 ||
      current.account_id.find('\0') != std::string::npos) {
    error = "invalid_binding_record"; return false;
  }
  BindingSafety safety;
  if(!read_safety(safety,error))return false;
  const bool protected_state=!current.account_id.empty()||current.revocation_requested||
      current.state==PlatformLinkBindingState::Bound;
  // Commit the monotonic Bound latch before publishing a bound cache/authority.
  if((protected_state&&!safety.ever_bound)||!safety.present)
    if(!write_safety(true,error))return false;
  JsonDocument document;
  document["platformOrigin"] = platform_base_url();
  document["profileRevision"] = profile_revision();
  document["bindingEpoch"] = current.binding_epoch;
  document["revocationRequested"] = current.revocation_requested;
  document["bindingClaimRequired"] = current.binding_claim_required;
  document["accountId"] = current.account_id;
  document["accountDisplayName"] = current.account_display_name;
  document["accountAvatarUrl"] = current.account_avatar_url;
  document["accountCanManageHardware"] = current.account_can_manage_hardware;
  document["avatarResourceAvailable"] = current.account_avatar_resource_available;
  document["avatarResourceRevision"] = current.account_avatar_resource_revision;
  document["avatarResourceUrl"] = current.account_avatar_resource_url;
  document["avatarResourceFormat"] = current.account_avatar_resource_format;
  document["avatarResourceWidth"] = current.account_avatar_resource_width;
  document["avatarResourceHeight"] = current.account_avatar_resource_height;
  document["avatarMaximumBytes"] = current.account_avatar_resource_maximum_bytes;
  std::string serialized;
  serializeJson(document, serialized);
  if (serialized.size() > 4096) { error = "binding_record_too_large"; return false; }
  if (serialized != g_persisted) {
    if (!settings_set_string(kNamespace, kKey, serialized)) {
      error = "binding_commit_failed"; return false;
    }
    g_persisted = std::move(serialized);
  }
  return true;
}

bool prepare_environment_cache_reset() {
  const auto current = pairing_snapshot();
  if (current.persistence_pending || current.revocation_requested) return false;
  if (!clear_cloud_role_intent()) return false;
  std::lock_guard<std::mutex> lock(g_cache_mutex);
  // Write a portable empty tombstone before publishing a different profile.
  // A missing origin/credential tag cannot restore authority after a power failure.
  // Explicit local server selection clears this server's local cache. It does
  // not request an account unbind from an unreachable previous server.
  const std::string tombstone = R"({"accountId":""})";
  if (!settings_set_string(kNamespace, kKey, tombstone)) return false;
  g_persisted = tombstone;
  return true;
}

} // namespace lunaredu::lunaros::platform_link
