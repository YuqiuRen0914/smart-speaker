#include "account_resources_internal.h"

#include <algorithm>
#include <utility>

#include "lunaredu_lunaros/delivery/resources.h"
#include "lunaredu_lunaros/persistence/settings.h"

namespace lunaredu::account_resources {
using namespace lunaros;
namespace {

constexpr const char *kPetCacheSettingsNamespace = "lunar_pet_cache";
constexpr const char *kPetCacheManifestKey = "manifest";
std::string g_cached_manifest_serialized;

} // namespace

void load_cache() {
  const std::string serialized =
      settings_get_string(kPetCacheSettingsNamespace, kPetCacheManifestKey);
  if (serialized.empty()) {
    return;
  }
  JsonDocument document;
  if (deserializeJson(document, serialized)) {
    (void)settings_erase(kPetCacheSettingsNamespace, kPetCacheManifestKey);
    return;
  }
  g_cached_manifest_serialized = serialized;

  AccountResourcesSnapshot cached;
  cached.binding_epoch = document["bindingEpoch"] | 0;
  cached.account_id = document["accountId"] | "";
  if (!cached.account_id.empty()) {
    cached.state = DeviceLinkBindingState::Bound;
  }
  cached.account_display_name = document["accountDisplayName"] | "";
  cached.account_avatar_url = document["accountAvatarUrl"] | "";
  cached.account_can_manage_hardware =
      document["accountCanManageHardware"] | false;
  cached.account_avatar_resource_available =
      document["avatarResourceAvailable"] | false;
  cached.account_avatar_resource_revision =
      document["avatarResourceRevision"] | "";
  cached.account_avatar_resource_url = document["avatarResourceUrl"] | "";
  cached.account_avatar_resource_format = document["avatarResourceFormat"] | "";
  cached.account_avatar_resource_width = document["avatarResourceWidth"] | 0;
  cached.account_avatar_resource_height = document["avatarResourceHeight"] | 0;
  cached.account_avatar_resource_maximum_bytes =
      document["avatarMaximumBytes"] | 0;
  cached.companion_available = document["companionAvailable"] | false;
  cached.companion_status = document["companionStatus"] | "cached";
  cached.companion_id = document["companionId"] | "";
  cached.companion_slug = document["companionSlug"] | "";
  cached.companion_name = document["companionName"] | "";
  cached.companion_species_name = document["companionSpeciesName"] | "";
  cached.companion_title = document["companionTitle"] | "";
  cached.companion_personality = document["companionPersonality"] | "";
  cached.companion_level = document["companionLevel"] | 0;
  cached.companion_animation_available = document["animationAvailable"] | false;
  cached.companion_resource_revision = document["resourceRevision"] | "";
  cached.companion_frame_url_template = document["frameUrlTemplate"] | "";
  cached.companion_resource_format = document["resourceFormat"] | "";
  cached.companion_frame_width = document["frameWidth"] | 0;
  cached.companion_frame_height = document["frameHeight"] | 0;
  cached.companion_frame_maximum_bytes = document["maximumFrameBytes"] | 0;
  for (JsonObjectConst animation :
       document["animations"].as<JsonArrayConst>()) {
    if (cached.companion_animation_count >=
        cached.companion_animations.size()) {
      break;
    }
    auto &target =
        cached.companion_animations[cached.companion_animation_count++];
    target.key = animation["key"] | "";
    target.frame_count = animation["frameCount"] | 0;
    target.frame_duration_ms = animation["frameDurationMs"] | 0;
  }
  replace_snapshot(std::move(cached));
}

void persist_cache() {
  const AccountResourcesSnapshot current = snapshot();
  if (current.state != DeviceLinkBindingState::Bound ||
      current.account_id.empty()) {
    if (!g_cached_manifest_serialized.empty()) {
      (void)settings_erase(kPetCacheSettingsNamespace, kPetCacheManifestKey);
      g_cached_manifest_serialized.clear();
    }
    return;
  }

  JsonDocument document;
  document["bindingEpoch"] = current.binding_epoch;
  document["accountId"] = current.account_id;
  document["accountDisplayName"] = current.account_display_name;
  document["accountAvatarUrl"] = current.account_avatar_url;
  document["accountCanManageHardware"] = current.account_can_manage_hardware;
  document["avatarResourceAvailable"] =
      current.account_avatar_resource_available;
  document["avatarResourceRevision"] = current.account_avatar_resource_revision;
  document["avatarResourceUrl"] = current.account_avatar_resource_url;
  document["avatarResourceFormat"] = current.account_avatar_resource_format;
  document["avatarResourceWidth"] = current.account_avatar_resource_width;
  document["avatarResourceHeight"] = current.account_avatar_resource_height;
  document["avatarMaximumBytes"] =
      current.account_avatar_resource_maximum_bytes;
  document["companionAvailable"] = current.companion_available;
  document["companionStatus"] = current.companion_status;
  document["companionId"] = current.companion_id;
  document["companionSlug"] = current.companion_slug;
  document["companionName"] = current.companion_name;
  document["companionSpeciesName"] = current.companion_species_name;
  document["companionTitle"] = current.companion_title;
  document["companionPersonality"] = current.companion_personality;
  document["companionLevel"] = current.companion_level;
  document["animationAvailable"] = current.companion_animation_available;
  document["resourceRevision"] = current.companion_resource_revision;
  document["frameUrlTemplate"] = current.companion_frame_url_template;
  document["resourceFormat"] = current.companion_resource_format;
  document["frameWidth"] = current.companion_frame_width;
  document["frameHeight"] = current.companion_frame_height;
  document["maximumFrameBytes"] = current.companion_frame_maximum_bytes;
  JsonArray animations = document["animations"].to<JsonArray>();
  for (size_t index = 0; index < current.companion_animation_count; ++index) {
    JsonObject target = animations.add<JsonObject>();
    target["key"] = current.companion_animations[index].key;
    target["frameCount"] = current.companion_animations[index].frame_count;
    target["frameDurationMs"] =
        current.companion_animations[index].frame_duration_ms;
  }
  std::string serialized;
  serializeJson(document, serialized);
  if (!serialized.empty() && serialized != g_cached_manifest_serialized) {
    if (settings_set_string(kPetCacheSettingsNamespace, kPetCacheManifestKey,
                            serialized)) {
      g_cached_manifest_serialized = std::move(serialized);
    }
  }
}

void erase_cache() {
  (void)settings_erase(kPetCacheSettingsNamespace, kPetCacheManifestKey);
  g_cached_manifest_serialized.clear();
}

} // namespace lunaredu::account_resources
