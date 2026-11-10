#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace lunaredu::lunaros::package {
struct Envelope {
  uint32_t version = 0;
  size_t manifest_bytes = 0, entry_offset = 0, entry_bytes = 0;
};
// Bounds are supplied by the host. This parser grants no execution permission.
bool envelope(const uint8_t *, size_t, uint32_t version, size_t maximum_bytes,
              Envelope &, std::string &error);
struct TargetProfile {
  const char *target, *cpu, *runtime_revision, *protection_profile;
  uint32_t abi, format;
};
enum class FieldFlag { Missing, False, True, Invalid };
// Borrowed immutable values; validation never retains their storage.
struct ArtifactBinding {
  std::string_view id{}, format{}, entry_sha256{}, source_sha256{};
  FieldFlag experimental = FieldFlag::Missing;
  bool target_present = false, target_versions_valid = false;
  std::string_view target{}, cpu{}, runtime_revision{}, protection_profile{};
  uint32_t abi = 0, aot_format = 0;
};
struct SignatureBinding {
  std::string_view schema, algorithm, key_id, value;
};
bool digest_valid(std::string_view);
bool artifact_id_valid(std::string_view);
bool standard_binding(const ArtifactBinding &, std::string_view backend, std::string_view source);
bool aot_binding(const ArtifactBinding &, std::string_view backend, std::string_view source,
                 const TargetProfile *);
bool signature_binding(const SignatureBinding &);
}
