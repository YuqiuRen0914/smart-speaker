#pragma once
#include "lunaredu_lunaros/device_link/device_link.h"
#include <array>

namespace lunaredu::account_resources {
struct AccountAnimationResource {
  std::string key;
  uint8_t frame_count = 0;
  uint16_t frame_duration_ms = 0;
};

inline constexpr size_t kAccountAnimationResourceCount = 5;

// Product-facing account assets; never part of the OS binding contract.
struct AccountResourcesSnapshot : lunaros::DeviceLinkPairingSnapshot {
  bool account_avatar_resource_available = false;
  std::string account_avatar_resource_revision;
  std::string account_avatar_resource_url;
  std::string account_avatar_resource_format;
  uint16_t account_avatar_resource_width = 0;
  uint16_t account_avatar_resource_height = 0;
  size_t account_avatar_resource_maximum_bytes = 0;
  bool companion_available = false;
  std::string companion_status;
  std::string companion_error_code;
  std::string companion_error_message;
  std::string companion_id;
  std::string companion_slug;
  std::string companion_name;
  std::string companion_species_name;
  std::string companion_title;
  std::string companion_personality;
  std::string companion_preview_url;
  bool companion_animation_available = false;
  std::string companion_resource_revision;
  std::string companion_frame_url_template;
  std::string companion_resource_format;
  uint16_t companion_frame_width = 0;
  uint16_t companion_frame_height = 0;
  size_t companion_frame_maximum_bytes = 0;
  size_t companion_animation_count = 0;
  std::array<AccountAnimationResource, kAccountAnimationResourceCount>
      companion_animations;
  int companion_level = 0;
};
AccountResourcesSnapshot snapshot();
} // namespace lunaredu::account_resources
