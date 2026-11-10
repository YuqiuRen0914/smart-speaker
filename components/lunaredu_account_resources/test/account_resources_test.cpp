#include "device_link/device_link_state.h"
#include "lunaredu_account_resources.h"
#include "lunaredu_account_resources_json.h"
#include "lunaredu_lunaros/delivery/resources.h"
#include "lunaredu_lunaros/network/network.h"
#include "lunaredu_lunaros/persistence/settings.h"
#include <cassert>
#include <cstdio>
#include <map>
using namespace lunaredu;
namespace {
std::map<std::string, std::string> settings;
bool writes_succeed = true;
int writes = 0, requests = 0;
std::string scope;
uint32_t epoch = 0;
std::string key(const char *ns, const char *name) {
  return std::string(ns) + "/" + name;
}
} // namespace
namespace lunaredu::lunaros {
std::string settings_get_string(const char *ns, const char *name,
                                const char *fallback) {
  const auto it = settings.find(key(ns, name));
  return it == settings.end() ? fallback : it->second;
}
bool settings_set_string(const char *ns, const char *name,
                         const std::string &value) {
  ++writes;
  if (!writes_succeed)
    return false;
  settings[key(ns, name)] = value;
  return true;
}
bool settings_erase(const char *ns, const char *name) {
  settings.erase(key(ns, name));
  return true;
}
void set_private_resource_scope(uint32_t value, const std::string &account) {
  epoch = value;
  scope = account;
}
bool request_resource(ResourceDescriptor value, int) {
  assert(value.id == "account-avatar");
  ++requests;
  return true;
}
bool device_cloud_credential_ready(size_t) { return true; }
NetworkLinkState network_link_state() { return NetworkLinkState::Down; }
NetworkSnapshot network_snapshot() { return {}; }
void report_network_device_link(NetworkDeviceLinkState, const char *) {}
namespace internal {
uint32_t system_monotonic_ms() { return 100; }
} // namespace internal
} // namespace lunaredu::lunaros
int main() {
  using namespace lunaredu::lunaros;
  auto observer = account_resources::observer();
  DeviceLinkConfig config;
  config.observer = &observer;
  device_link::configure_runtime(config);
  settings["lunar_pet_cache/manifest"] =
      R"({"bindingEpoch":3,"accountId":"A","accountDisplayName":"Alice","companionAvailable":true,"companionName":"Cached","companionPersonality":"quiet"})";
  device_link::load_cached_binding();
  auto binding = device_link::pairing_snapshot();
  observer.restore(binding);
  assert(binding.account_id == "A" && scope == "A" && epoch == 3);
  assert(account_resources::snapshot().companion_name == "Cached");
  assert(!settings["lunar_link/binding"].empty());
  assert(settings["lunar_link/binding"].find("companion") == std::string::npos);

  JsonDocument data;
  auto present = [&](const char *json) {
    assert(!deserializeJson(data, json));
    observer.presence(device_link::pairing_snapshot(),
                      data.as<JsonVariantConst>());
  };
  present(
      R"({"companion":{"mode":"pet","pet":{"id":"pet-a","name":"Online","personality":"playful"}},"companionState":{"status":"ready"},"resourceManifest":{"avatar":{"revision":"a1","url":"https://assets/a","format":"rgb666-u8x3","width":96,"height":96,"maximumBytes":27648},"companion":{"sourceRevision":"p1","frameUrlTemplate":"https://assets/p/{frame}","format":"a8rgb666-u8x4","width":144,"height":144,"maximumFrameBytes":82944,"animations":[{"key":"happy","frameCount":4,"frameDurationMs":100}]}}})");
  auto online = account_resources::snapshot();
  assert(online.companion_name == "Online" &&
         online.companion_animation_available);
  assert(online.account_avatar_resource_available && requests == 1);
  assert(online.companion_animations[0].frame_count == 4);
  present(
      R"({"resourceManifest":{"avatar":{"revision":"partial"},"companion":{"sourceRevision":"partial"}}})");
  assert(account_resources::snapshot().account_avatar_resource_revision ==
         "a1");
  assert(account_resources::snapshot().companion_resource_revision == "p1");

  writes_succeed = false;
  present(R"({"companion":{"mode":"pet","pet":{"name":"Retry"}}})");
  const int failed_write_count = writes;
  writes_succeed = true;
  present(R"({"companion":{"mode":"pet","pet":{"name":"Retry"}}})");
  assert(writes > failed_write_count);
  assert(settings["lunar_pet_cache/manifest"].find("Retry") !=
         std::string::npos);

  present(R"({"resourceManifest":{"avatar":null,"companion":null}})");
  assert(!account_resources::snapshot().companion_available);
  assert(!account_resources::snapshot().account_avatar_resource_available);
  device_link::update_pairing_snapshot(DeviceLinkBindingState::Bound, "", "", 0,
                                       "", 4, "B");
  auto changed = account_resources::snapshot();
  assert(changed.account_id == "B" && changed.account_display_name.empty());
  assert(!changed.companion_animation_available &&
         !changed.account_avatar_resource_available);
  assert(scope == "B" && epoch == 4);
  observer.presence(
      binding,
      data.as<JsonVariantConst>()); // Delayed A cannot restore private state.
  assert(account_resources::snapshot().account_id == "B");

  present(
      R"({"companion":{"mode":"pet","pet":{"name":"Account B"}},"resourceManifest":{"avatar":{"revision":"b1","url":"https://assets/b","format":"rgb666-u8x3","width":96,"height":96,"maximumBytes":27648}}})");
  assert(account_resources::snapshot().account_avatar_resource_available);
  device_link::update_pairing_snapshot(DeviceLinkBindingState::Bound, "", "", 0,
                                       "", 5, "B");
  assert(account_resources::snapshot().account_id == "B");
  assert(account_resources::snapshot().companion_name.empty());
  assert(!account_resources::snapshot().account_avatar_resource_available);

  device_link::update_pairing_snapshot(DeviceLinkBindingState::Unbound, "", "",
                                       0, "", 6);
  assert(account_resources::snapshot().account_id.empty());
  assert(settings.count("lunar_pet_cache/manifest") == 0);
  // A stale legacy manifest must not override the persisted unbind tombstone.
  settings["lunar_pet_cache/manifest"] =
      R"({"accountId":"A","bindingEpoch":3,"companionName":"stale"})";
  device_link::load_cached_binding();
  observer.restore(device_link::pairing_snapshot());
  assert(device_link::pairing_snapshot().account_id.empty());
  assert(account_resources::snapshot().companion_name.empty());
  assert(scope.empty() && epoch == 6);
  config.observer = nullptr;
  device_link::configure_runtime(config);
  device_link::update_pairing_snapshot(DeviceLinkBindingState::Bound, "", "", 0,
                                       "", 7, "C");
  assert(device_link::pairing_snapshot().account_id == "C");
  JsonDocument runtime;
  device_link::add_runtime_state(runtime.to<JsonObject>());
  assert(runtime["localAddress"].is<std::string>());
  assert(runtime["servoReady"].isUnbound());
  observer.write_runtime = [](JsonObject state) {
    state["productReady"] = true;
  };
  config.observer = &observer;
  device_link::configure_runtime(config);
  device_link::add_runtime_state(runtime.as<JsonObject>());
  assert(runtime["productReady"].as<bool>());
  std::puts("Binding migration, optional projection, offline assets, "
            "partial/null manifests, retry, account/epoch invalidation and "
            "stale callbacks passed");
}
