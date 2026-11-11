#pragma once

#include <string_view>
#include "pet_animation_pack.h"

namespace smart_speaker::product {
inline constexpr std::string_view kPetActionNames[] = {
#include "pet_action_names.inc"
};
static_assert(sizeof(kPetActionNames) / sizeof(kPetActionNames[0]) == kPetClipCount);

constexpr PetClip pet_action_named(std::string_view name) {
  for (size_t i = 0; i < kPetClipCount; ++i)
    if (kPetActionNames[i] == name) return static_cast<PetClip>(i);
  return PetClip::Count;
}
} // namespace smart_speaker::product
