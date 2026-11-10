#include "persistence/application_artifact_internal.h"
#include "app/execution/backend_catalog.h"
#include "persistence/application_artifact_json.h"
#include "app/package/application_limits.h"
#include "app/execution/application_backend.h"
#include <cstring>
#include "app/package/package_format.h"
#include "persistence/application_binding_json.h"

namespace lunaredu::lunaros::apps {
namespace {
using package::digest_valid;
using package::json::signature_binding;
bool standard_binding(JsonVariantConst a, const Application &app, const std::string &source) {
  return package::json::standard_binding(a,app.backend,source);
}
bool aot_binding(JsonVariantConst a, const Application &app, const std::string &source) {
  return package::json::aot_binding(a,app.backend,source,application_artifact_profile("aot"));
}
}
bool validate_package_artifact(JsonVariantConst m, uint32_t version,
                               const Application &app, std::string &error, bool aot) {
  const std::string source = m["entry_sha256"] | "";
  if (version != kApplicationPlatform.package_version || !m["artifact"].is<JsonObjectConst>() ||
      !(standard_binding(m["artifact"], app, source) ||
        (aot && aot_binding(m["artifact"],app,m["artifact"]["source_sha256"]|"")
          && std::string(m["artifact"]["entry_sha256"]|"")==source))) {
    error = "设备不支持此执行产物";
    return false;
  }
  return true;
}
bool select_release_artifact(JsonVariantConst release, Application &app,
                                     bool wasm, bool python, std::string &error, bool aot) {
  auto artifacts = release["artifacts"].as<JsonArrayConst>();
  const std::string source = release["application"]["entry_sha256"] | "";
  if (strcmp(release["schema"] | "", "lunaros.release/v1") || artifacts.isNull() ||
      !artifacts.size() || artifacts.size() > 8 || !digest_valid(source)) {
    error = "应用发行清单无效"; return false;
  }
  if (!((app.backend == "wasm" && (wasm || aot)) || (app.backend == "micropython" && python))) {
    error = "此设备未启用应用后端"; return false;
  }
  // Prefer one compatible signed AOT. Fall back only before download/launch.
  for (unsigned pass=0;pass<2;++pass) for (JsonVariantConst a : artifacts) {
    const bool optimized=pass==0;
    if (optimized ? (!aot || !aot_binding(a,app,source) || !signature_binding(a))
                  : (!standard_binding(a, app, source) || (app.backend=="wasm"?!wasm:!python))) continue;
    const uint32_t version = a["package_version"] | 0U;
    const uint32_t size = a["size"] | 0U;
    const std::string hash = a["sha256"] | "", url = a["package_url"] | "";
    if (version != kApplicationPlatform.package_version || size < 24 || size > kMaximumPackageBytes ||
        !digest_valid(hash) || url.size() <= 10 || url.size() > 180 ||
        url.rfind("/packages/", 0) != 0 || url.find("..") != std::string::npos ||
        url.size() < 5 || url.substr(url.size() - 5) != ".lapp") continue;
    bool safe = true;
    for (size_t i = 10; i < url.size(); ++i) {
      char c = url[i];
      if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) safe = false;
    }
    if (!safe) continue;
    app.size = size; app.sha256 = hash; app.package_url = url;
    app.artifact_id = a["id"] | ""; app.source_sha256 = source;
    app.entry_format = optimized?"aot":app.backend; app.entry_sha256 = a["entry_sha256"]|"";
    app.signer_id=optimized?std::string(a["signature"]["key_id"]|""):"";
    app.package_signature=optimized?std::string(a["signature"]["value"]|""):"";
    app.package_version = version;
    return true;
  }
  error = "没有兼容的应用产物"; return false;
}
} // namespace lunaredu::lunaros::apps
