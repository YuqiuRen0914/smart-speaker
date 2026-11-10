#include "device_link/device_link_state.h"

#include <algorithm>
#include <utility>

#include "lunaredu_lunaros/delivery/resources.h"
#include "lunaredu_lunaros/persistence/settings.h"

namespace lunaredu::lunaros::device_link {
namespace {

constexpr const char *kNamespace = "lunar_link";
constexpr const char *kKey = "binding";
std::string g_persisted;

} // namespace

void load_cached_binding() {
  std::string serialized = settings_get_string(kNamespace, kKey);
  // Read-only migration of deployed binding identity; product assets are restored
  // independently by their owner. Do not erase that owner's legacy manifest.
  if (serialized.empty()) {
    serialized = settings_get_string("lunar_pet_cache", "manifest");
  }
  if (serialized.empty()) {
    return;
  }
  JsonDocument document;
  if (deserializeJson(document, serialized)) {
    return;
  }
  DeviceLinkPairingSnapshot cached;
  cached.binding_epoch = document["bindingEpoch"] | 0;
  cached.account_id = document["accountId"] | "";
  cached.state = cached.account_id.empty() ? DeviceLinkBindingState::Unbound
                                           : DeviceLinkBindingState::Bound;
  cached.account_display_name = document["accountDisplayName"] | "";
  cached.account_avatar_url = document["accountAvatarUrl"] | "";
  cached.account_can_manage_hardware = document["accountCanManageHardware"] | false;
  set_private_resource_scope(
      static_cast<uint32_t>(std::max(0, cached.binding_epoch)), cached.account_id);
  replace_pairing_snapshot(std::move(cached));
  persist_cached_binding();
}

void persist_cached_binding() {
  const auto current = pairing_snapshot();
  JsonDocument document;
  document["bindingEpoch"] = current.binding_epoch;
  document["accountId"] = current.account_id;
  document["accountDisplayName"] = current.account_display_name;
  document["accountAvatarUrl"] = current.account_avatar_url;
  document["accountCanManageHardware"] = current.account_can_manage_hardware;
  std::string serialized;
  serializeJson(document, serialized);
  if (serialized != g_persisted &&
      settings_set_string(kNamespace, kKey, serialized)) {
    g_persisted = std::move(serialized);
  }
}

void erase_cached_binding() {
  // Persist the empty binding as a tombstone, preventing legacy cache resurrection.
  g_persisted.clear();
  persist_cached_binding();
}

} // namespace lunaredu::lunaros::device_link
