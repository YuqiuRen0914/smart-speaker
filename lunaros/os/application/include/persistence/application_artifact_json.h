#pragma once
#include "app/package/application_package.h"
#include <ArduinoJson.h>

namespace lunaredu::lunaros::apps {
// A v2 store item is a release. Prefer compatible signed AOT when enabled;
// otherwise select one standard artifact without downloading other variants.
bool select_release_artifact(JsonVariantConst release, Application &,
                                     bool wasm_available, bool python_available,
                                     std::string &error, bool aot_available = false);
} // namespace lunaredu::lunaros::apps
