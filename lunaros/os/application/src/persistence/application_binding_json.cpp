#include "persistence/application_binding_json.h"
namespace lunaredu::lunaros::package::json {
namespace {
ArtifactBinding decode(JsonVariantConst a) {
  ArtifactBinding result;
  result.id=a["id"]|""; result.format=a["format"]|"";
  result.entry_sha256=a["entry_sha256"]|""; result.source_sha256=a["source_sha256"]|"";
  const auto experimental=a["experimental"];
  result.experimental=experimental.isUnbound()?FieldFlag::Missing:
    !experimental.is<bool>()?FieldFlag::Invalid:experimental.as<bool>()?FieldFlag::True:FieldFlag::False;
  const auto t=a["target"];
  result.target_present=!t.isUnbound();
  result.target=t["target"]|""; result.cpu=t["cpu"]|"";
  result.runtime_revision=t["runtime_revision"]|""; result.protection_profile=t["protection_profile"]|"";
  result.target_versions_valid=t["abi"].is<uint32_t>() && t["aot_format"].is<uint32_t>();
  result.abi=t["abi"]|0U; result.aot_format=t["aot_format"]|0U;
  return result;
}
}
bool standard_binding(JsonVariantConst a,std::string_view backend,std::string_view source) {
  return package::standard_binding(decode(a),backend,source);
}
bool aot_binding(JsonVariantConst a,std::string_view backend,std::string_view source,const TargetProfile *profile) {
  return package::aot_binding(decode(a),backend,source,profile);
}
bool signature_binding(JsonVariantConst a) {
  const auto s=a["signature"];
  return package::signature_binding({s["schema"]|"",s["algorithm"]|"",s["key_id"]|"",s["value"]|""});
}
}
