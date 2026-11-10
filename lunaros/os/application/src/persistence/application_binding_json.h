#pragma once
#include <ArduinoJson.h>
#include "app/package/package_format.h"
namespace lunaredu::lunaros::package::json {
// Wire adapter. Borrowed fields remain valid only during synchronous validation.
bool standard_binding(JsonVariantConst, std::string_view backend, std::string_view source);
bool aot_binding(JsonVariantConst, std::string_view backend, std::string_view source, const TargetProfile *);
bool signature_binding(JsonVariantConst);
}
