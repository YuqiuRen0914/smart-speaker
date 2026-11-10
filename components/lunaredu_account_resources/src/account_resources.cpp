#include "account_resources_internal.h"
#include "lunaredu_lunaros/delivery/resources.h"
#include <algorithm>
#include <mutex>
#include <utility>

namespace lunaredu::account_resources {
using namespace lunaros;
namespace {
std::mutex g_pairing_mutex;
std::mutex g_update_mutex;
uint32_t g_binding_revision = 0;
bool g_binding_observed = false;
AccountResourcesSnapshot g_pairing_snapshot;
void clear_companion_state_locked() {
  g_pairing_snapshot.companion_available = false;
  g_pairing_snapshot.companion_status = "ai_only";
  g_pairing_snapshot.companion_error_code.clear();
  g_pairing_snapshot.companion_error_message.clear();
  g_pairing_snapshot.companion_id.clear();
  g_pairing_snapshot.companion_slug.clear();
  g_pairing_snapshot.companion_name.clear();
  g_pairing_snapshot.companion_species_name.clear();
  g_pairing_snapshot.companion_title.clear();
  g_pairing_snapshot.companion_personality.clear();
  g_pairing_snapshot.companion_preview_url.clear();
  g_pairing_snapshot.companion_level = 0;
  g_pairing_snapshot.companion_animation_available = false;
  g_pairing_snapshot.companion_resource_revision.clear();
  g_pairing_snapshot.companion_frame_url_template.clear();
  g_pairing_snapshot.companion_resource_format.clear();
  g_pairing_snapshot.companion_frame_width = 0;
  g_pairing_snapshot.companion_frame_height = 0;
  g_pairing_snapshot.companion_frame_maximum_bytes = 0;
  g_pairing_snapshot.companion_animation_count = 0;
  for (auto &animation : g_pairing_snapshot.companion_animations) {
    animation = {};
  }
}

void clear_account_resource_state_locked() {
  g_pairing_snapshot.account_avatar_resource_available = false;
  g_pairing_snapshot.account_avatar_resource_revision.clear();
  g_pairing_snapshot.account_avatar_resource_url.clear();
  g_pairing_snapshot.account_avatar_resource_format.clear();
  g_pairing_snapshot.account_avatar_resource_width = 0;
  g_pairing_snapshot.account_avatar_resource_height = 0;
  g_pairing_snapshot.account_avatar_resource_maximum_bytes = 0;
}

void update_resource_snapshot(JsonVariantConst companion_state,
                              JsonVariantConst manifest) {
  AccountResourcesSnapshot current;
  ResourceDescriptor avatar_descriptor;
  bool request_avatar = false;
  {
    std::lock_guard<std::mutex> lock(g_pairing_mutex);
    g_pairing_snapshot.companion_status = companion_state["status"] | "ai_only";
    g_pairing_snapshot.companion_error_code = companion_state["errorCode"] | "";
    g_pairing_snapshot.companion_error_message =
        companion_state["message"] | "";

    const JsonObjectConst manifest_object = manifest.as<JsonObjectConst>();
    const JsonVariantConst avatar = manifest_object["avatar"];
    if (!avatar.isUnbound()) {
      if (avatar.isNull()) {
        // An explicit null is the server's intentional "no avatar" result.
        clear_account_resource_state_locked();
      } else {
        const std::string revision = avatar["revision"] | "";
        const std::string url = avatar["url"] | "";
        const std::string format = avatar["format"] | "";
        const uint16_t width = avatar["width"] | 0;
        const uint16_t height = avatar["height"] | 0;
        const size_t maximum_bytes = avatar["maximumBytes"] | 0;
        const bool complete =
            !revision.empty() && !url.empty() && format == "rgb666-u8x3" &&
            width == 96 && height == 96 &&
            maximum_bytes >= static_cast<size_t>(width) * height * 3;
        if (complete) {
          g_pairing_snapshot.account_avatar_resource_available = true;
          g_pairing_snapshot.account_avatar_resource_revision = revision;
          g_pairing_snapshot.account_avatar_resource_url = url;
          g_pairing_snapshot.account_avatar_resource_format = format;
          g_pairing_snapshot.account_avatar_resource_width = width;
          g_pairing_snapshot.account_avatar_resource_height = height;
          g_pairing_snapshot.account_avatar_resource_maximum_bytes =
              maximum_bytes;
        }
        // Partial descriptors are transient platform state.  Retain the last
        // complete descriptor so a downloaded avatar remains usable offline.
      }
    }

    const JsonVariantConst companion = manifest_object["companion"];
    if (!companion.isUnbound()) {
      if (companion.isNull()) {
        clear_companion_state_locked();
      } else {
        const std::string revision = companion["sourceRevision"] | "";
        const std::string frame_url = companion["frameUrlTemplate"] | "";
        const std::string format = companion["format"] | "";
        const uint16_t width = companion["width"] | 0;
        const uint16_t height = companion["height"] | 0;
        const size_t maximum_bytes = companion["maximumFrameBytes"] | 0;
        size_t animation_count = 0;
        std::array<AccountAnimationResource, kAccountAnimationResourceCount>
            animations{};
        for (JsonObjectConst animation :
             companion["animations"].as<JsonArrayConst>()) {
          if (animation_count >= animations.size()) {
            break;
          }
          AccountAnimationResource &target = animations[animation_count++];
          target.key = animation["key"] | "";
          target.frame_count = animation["frameCount"] | 0;
          target.frame_duration_ms = animation["frameDurationMs"] | 0;
        }
        const bool complete =
            !revision.empty() && !frame_url.empty() &&
            format == "a8rgb666-u8x4" && width == 144 && height == 144 &&
            maximum_bytes >= static_cast<size_t>(width) * height * 4 &&
            animation_count != 0;
        if (complete) {
          g_pairing_snapshot.companion_animation_available = true;
          g_pairing_snapshot.companion_resource_revision = revision;
          g_pairing_snapshot.companion_frame_url_template = frame_url;
          g_pairing_snapshot.companion_resource_format = format;
          g_pairing_snapshot.companion_frame_width = width;
          g_pairing_snapshot.companion_frame_height = height;
          g_pairing_snapshot.companion_frame_maximum_bytes = maximum_bytes;
          g_pairing_snapshot.companion_animation_count = animation_count;
          for (size_t index = 0; index < animations.size(); ++index) {
            g_pairing_snapshot.companion_animations[index] = animations[index];
          }
        }
        // As with the avatar, do not erase a complete cached animation when
        // the platform sends a partial resource projection.
      }
    }
    ++g_pairing_snapshot.revision;
    current = g_pairing_snapshot;

    if (current.account_avatar_resource_available) {
      avatar_descriptor.id = "account-avatar";
      avatar_descriptor.revision = current.account_avatar_resource_revision;
      avatar_descriptor.url = current.account_avatar_resource_url;
      avatar_descriptor.format = current.account_avatar_resource_format;
      avatar_descriptor.width = current.account_avatar_resource_width;
      avatar_descriptor.height = current.account_avatar_resource_height;
      avatar_descriptor.maximum_bytes =
          current.account_avatar_resource_maximum_bytes;
      avatar_descriptor.scope = ResourceScope::AccountPrivate;
      request_avatar = true;
    }
  }

  if (request_avatar) {
    (void)request_resource(std::move(avatar_descriptor), 20);
  }
}

void update_companion_snapshot(JsonVariantConst companion) {
  std::lock_guard<std::mutex> lock(g_pairing_mutex);
  if (companion.isNull()) {
    // A missing companion projection is not the same as an explicit unbind.
    // Keep the cached pet identity while the platform is temporarily unable
    // to join the account/pet tables.
    return;
  }
  JsonVariantConst pet = companion["pet"];
  const std::string mode = companion["mode"] | "";
  const bool available = mode == "pet" && !pet.isNull();
  g_pairing_snapshot.companion_available = available;
  g_pairing_snapshot.companion_id = available ? (pet["id"] | "") : "";
  g_pairing_snapshot.companion_slug = available ? (pet["slug"] | "") : "";
  g_pairing_snapshot.companion_name = available ? (pet["name"] | "") : "";
  g_pairing_snapshot.companion_species_name =
      available ? (pet["speciesName"] | "") : "";
  g_pairing_snapshot.companion_title = available ? (pet["title"] | "") : "";
  g_pairing_snapshot.companion_personality =
      available ? (pet["personality"] | "") : "";
  g_pairing_snapshot.companion_preview_url =
      available ? (pet["previewUrl"] | "") : "";
  g_pairing_snapshot.companion_level = available ? (pet["level"] | 1) : 0;
  ++g_pairing_snapshot.revision;
}

} // namespace

void replace_snapshot(AccountResourcesSnapshot snapshot) {
  std::lock_guard<std::mutex> lock(g_pairing_mutex);
  snapshot.revision = g_pairing_snapshot.revision + 1;
  g_pairing_snapshot = std::move(snapshot);
}

AccountResourcesSnapshot snapshot() {
  std::lock_guard<std::mutex> lock(g_pairing_mutex);
  return g_pairing_snapshot;
}

namespace {
bool apply_binding(const DeviceLinkPairingSnapshot &binding) {
  bool clear_cache = false;
  {
    std::lock_guard<std::mutex> lock(g_pairing_mutex);
    if (g_binding_observed &&
        static_cast<int32_t>(binding.revision - g_binding_revision) < 0) {
      return false;
    }
    g_binding_revision = binding.revision;
    g_binding_observed = true;
    const bool changed =
        (!binding.account_id.empty() &&
         binding.account_id != g_pairing_snapshot.account_id) ||
        binding.binding_epoch != g_pairing_snapshot.binding_epoch;
    clear_cache = binding.state == DeviceLinkBindingState::Unbound;
    if (changed || clear_cache) {
      clear_account_resource_state_locked();
      clear_companion_state_locked();
    }
    const uint32_t revision = g_pairing_snapshot.revision + 1;
    static_cast<DeviceLinkPairingSnapshot &>(g_pairing_snapshot) = binding;
    g_pairing_snapshot.revision = revision;
  }
  if (clear_cache) {
    erase_cache();
  }
  return true;
}
void binding_changed(const DeviceLinkPairingSnapshot &binding) {
  std::lock_guard<std::mutex> lock(g_update_mutex);
  apply_binding(binding);
}
void restore(const DeviceLinkPairingSnapshot &binding) {
  std::lock_guard<std::mutex> lock(g_update_mutex);
  load_cache();
  apply_binding(binding);
}
void presence(const DeviceLinkPairingSnapshot &binding, JsonVariantConst data) {
  std::lock_guard<std::mutex> lock(g_update_mutex);
  if (!apply_binding(binding) ||
      binding.state != DeviceLinkBindingState::Bound ||
      binding.account_id.empty()) {
    return;
  }
  update_companion_snapshot(data["companion"]);
  update_resource_snapshot(data["companionState"], data["resourceManifest"]);
  persist_cache();
}
} // namespace
DeviceLinkObserver observer(void (*write_runtime)(JsonObject)) {
  return {write_runtime, binding_changed, restore, presence};
}

} // namespace lunaredu::account_resources
