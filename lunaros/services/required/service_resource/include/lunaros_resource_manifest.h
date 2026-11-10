#pragma once
#include <cstddef>

namespace lunaredu::lunaros {
// Immutable product boot metadata. Descriptor IDs must remain alive until reboot;
// downloading, readiness and runtime registration live in the Resource API.
enum class ManagedAssetFormat { TrueType, EspDl, SpeechPack };
struct ManagedAssetDefinition {
  const char *id = nullptr;
  ManagedAssetFormat format = ManagedAssetFormat::EspDl;
  // Optional SpeechPack family allowlist (prefix before '_'). The consumer
  // supplies immutable names with firmware lifetime; null/zero is unrestricted.
  const char *const *model_families = nullptr;
  size_t model_family_count = 0;
};
}
