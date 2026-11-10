#include "app/package/package_format.h"
#include <cstring>

namespace lunaredu::lunaros::package {
namespace {
uint32_t u32(const uint8_t *p) {
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i) value |= uint32_t(p[i]) << (8 * i);
  return value;
}
}
bool envelope(const uint8_t *bytes, size_t size, uint32_t version, size_t maximum,
              Envelope &out, std::string &error) {
  if (!bytes || size < 24 || size > maximum || std::memcmp(bytes,"LAPP",4) || u32(bytes+4)!=version) {
    error="应用包格式无效"; return false;
  }
  const size_t manifest=u32(bytes+8), entry=u32(bytes+12);
  // Subtraction keeps bounds valid on 32-bit targets as well as host platforms.
  if (manifest<2 || manifest>4096 || entry<8 || manifest>size-16 || entry!=size-16-manifest) {
    error="应用包不完整"; return false;
  }
  out={version,manifest,16+manifest,entry}; return true;
}
bool digest_valid(std::string_view s) {
  if(s.size()!=64)return false;
  for(char c:s)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
  return true;
}
bool artifact_id_valid(std::string_view s) {
  if(s.empty()||s.size()>64||s.front()<'a'||s.front()>'z')return false;
  for(char c:s)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'))return false;
  return true;
}
bool standard_binding(const ArtifactBinding &a,std::string_view backend,std::string_view source) {
  return artifact_id_valid(a.id) && a.format==backend && digest_valid(source) && a.entry_sha256==source &&
    a.source_sha256==source && !a.target_present &&
    (a.experimental==FieldFlag::Missing || a.experimental==FieldFlag::False);
}
bool aot_binding(const ArtifactBinding &a,std::string_view backend,std::string_view source,
                 const TargetProfile *profile) {
  if(!profile)return false;
  return backend=="wasm" && a.format=="aot" && artifact_id_valid(a.id) &&
    digest_valid(source) && a.source_sha256==source && digest_valid(a.entry_sha256) &&
    a.experimental==FieldFlag::True && a.target_present && a.target_versions_valid &&
    a.target==profile->target && a.cpu==profile->cpu &&
    a.runtime_revision==profile->runtime_revision && a.abi==profile->abi &&
    a.aot_format==profile->format && a.protection_profile==profile->protection_profile;
}
bool signature_binding(const SignatureBinding &s) {
  return s.schema=="lunaros.signature/v1" && s.algorithm=="ES256" &&
    artifact_id_valid(s.key_id) && s.value.size()==128 && digest_valid(s.value.substr(0,64)) && digest_valid(s.value.substr(64));
}
}
